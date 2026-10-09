import Foundation
import AVFoundation
import CoreML

struct VisionResult {let time,tti,confidence,processingMs:Double;let inPath:Bool}
final class Vision {
    private let core:LDCore
    private let detector:MLModel
    private let road:MLModel
    private let input:MLMultiArray
    init(core:LDCore) throws {
        self.core=core
        let config=MLModelConfiguration();config.computeUnits = .all
        guard let d=Bundle.main.url(forResource:"detector",withExtension:"mlmodelc"),let r=Bundle.main.url(forResource:"road",withExtension:"mlmodelc") else{throw AppError.message("Add detector.mlmodelc and road.mlmodelc to the app bundle")}
        detector=try MLModel(contentsOf:d,configuration:config);road=try MLModel(contentsOf:r,configuration:config)
        input=try MLMultiArray(shape:[1,3,320,320],dataType:.float32)
        for model in [detector,road] {
            guard let c=model.modelDescription.inputDescriptionsByName["images"]?.multiArrayConstraint,c.shape.map(\.intValue)==[1,3,320,320],c.dataType == .float32 else{throw AppError.message("Core ML images input must be float32 [1,3,320,320]")}
        }
    }
    private func compact(_ a:MLMultiArray)->[Float] {
        // Core ML may return padded/strided arrays. Respect logical strides.
        let shape=a.shape.map(\.intValue),strides=a.strides.map(\.intValue)
        let pointer=a.dataPointer.assumingMemoryBound(to:Float.self)
        return (0..<a.count).map {flat in
            var index=flat,offset=0
            for axis in stride(from:shape.count-1,through:0,by:-1) {offset += (index % shape[axis])*strides[axis];index /= shape[axis]}
            return pointer[offset]
        }
    }
    func process(_ frame:CVPixelBuffer,time:Double)throws->VisionResult {
        let start=clockNow()
        guard core.prepare(frame,tensor:input.dataPointer,bytes:input.count*4,gray:true) else{throw AppError.message(core.error())}
        let features=try MLDictionaryFeatureProvider(dictionary:["images":MLFeatureValue(multiArray:input)])
        let detection=try detector.prediction(from:features)
        let segmentation=try road.prediction(from:features)
        guard let d=detection.featureValue(for:"detections")?.multiArrayValue,
              let r=segmentation.featureValue(for:"drivable")?.multiArrayValue,
              let l=segmentation.featureValue(for:"lanes")?.multiArrayValue,
              [d,r,l].allSatisfy({$0.dataType == .float32}) else{throw AppError.message("Expected float32 detections/drivable/lanes outputs")}
        let ds=d.shape.map(\.intValue),rs=r.shape.map(\.intValue)
        guard ds.count==3,ds[0]==1,(ds[1]==84 || ds[2]==84),rs.count==4,rs[0]==1,rs[1]==2,rs==l.shape.map(\.intValue) else{throw AppError.message("Unsupported YOLOv8 / road output shape")}
        let cf=ds[1]==84,n=cf ? ds[2]:ds[1]
        let dv=compact(d),rv=compact(r),lv=compact(l)
        let h=dv.withUnsafeBufferPointer {dp in rv.withUnsafeBufferPointer {rp in lv.withUnsafeBufferPointer {lp in
            core.vision(time,detections:dp.baseAddress!,count:dv.count,candidates:Int32(n),channelFirst:cf,road:rp.baseAddress!,lane:lp.baseAddress!,width:Int32(rs[3]),height:Int32(rs[2]))
        }}}
        guard let h=h else{throw AppError.message(core.error())}
        return VisionResult(time:time,tti:h[0].doubleValue,confidence:h[1].doubleValue,processingMs:(clockNow()-start)*1000,inPath:h[2].boolValue)
    }
}
final class FrameSource:NSObject,AVCaptureVideoDataOutputSampleBufferDelegate {
    let session=AVCaptureSession()
    private let capture=DispatchQueue(label:"camera-capture")
    private let inference=DispatchQueue(label:"vision")
    private let lock=NSLock()
    private var pending:(CVPixelBuffer,Double)?
    private var running=false
    private var generation=0
    private var workerRunning=false
    private var vision:Vision?
    private let core:LDCore
    private var reader:AVAssetReader?
    private var lastFrame=0.0
    var onResult:((VisionResult)->Void)?
    var onError:((String)->Void)?
    init(core:LDCore){self.core=core;super.init()}
    // One retained pending CVPixelBuffer, replaced and released under the lock.
    private func offer(_ frame:CVPixelBuffer,time:Double) {
        lock.lock();guard running else{lock.unlock();return};pending=(frame,time)
        let start = !workerRunning;workerRunning=true;lock.unlock()
        if start {inference.async {[weak self] in self?.drain()}}
    }
    private func drain() {
        while true {
            lock.lock()
            guard running,let item=pending else{workerRunning=false;pending=nil;lock.unlock();return}
            let token=generation;pending=nil;lock.unlock()
            autoreleasepool {
                do {
                    if vision==nil {vision=try Vision(core:core)}
                    let result=try vision!.process(item.0,time:item.1)
                    lock.lock();let valid=running&&token==generation;lock.unlock()
                    if valid {onResult?(result)}
                }catch{onError?(error.localizedDescription)}
            }
        }
    }
    func camera() {
        stop();lock.lock();running=true;let token=generation;lock.unlock()
        AVCaptureDevice.requestAccess(for:.video) {[weak self] allowed in
            guard let self=self else{return}
            guard allowed else{self.onError?("Camera permission required");return}
            self.capture.async {
                do {
                    self.lock.lock();let active=self.running&&self.generation==token;self.lock.unlock();guard active else{return}
                    self.session.beginConfiguration();defer{self.session.commitConfiguration()}
                    self.session.inputs.forEach {self.session.removeInput($0)};self.session.outputs.forEach {self.session.removeOutput($0)}
                    self.session.sessionPreset = .hd1280x720
                    guard let device=AVCaptureDevice.default(.builtInWideAngleCamera,for:.video,position:.back) else{throw AppError.message("Rear camera unavailable")}
                    let input=try AVCaptureDeviceInput(device:device)
                    guard self.session.canAddInput(input) else{throw AppError.message("Cannot attach camera")};self.session.addInput(input)
                    let output=AVCaptureVideoDataOutput();output.alwaysDiscardsLateVideoFrames=true
                    output.videoSettings=[kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_420YpCbCr8BiPlanarFullRange]
                    output.setSampleBufferDelegate(self,queue:self.capture)
                    guard self.session.canAddOutput(output) else{throw AppError.message("Cannot attach analyzer")};self.session.addOutput(output)
                    output.connection(with:.video)?.videoOrientation = .landscapeRight
                    self.capture.async {self.lock.lock();let active=self.running&&self.generation==token;self.lock.unlock();if active{self.session.startRunning()}}
                }catch{self.onError?(error.localizedDescription)}
            }
        }
    }
    func captureOutput(_ output:AVCaptureOutput,didOutput sampleBuffer:CMSampleBuffer,from connection:AVCaptureConnection) {
        let t=clockNow(),thermal=ProcessInfo.processInfo.thermalState
        guard thermal != .critical && thermal != .serious else{return}
        let interval=thermal == .fair ? 1.0/15:1.0/30
        guard t-lastFrame>=interval,let f=CMSampleBufferGetImageBuffer(sampleBuffer) else{return}
        let captured=CMSampleBufferGetPresentationTimeStamp(sampleBuffer).seconds
        guard abs(t-captured)<5 else{onError?("Camera timestamp is not on the uptime clock");return}
        lastFrame=t;offer(f,time:captured)
    }
    func video(_ url:URL) {
        stop();lock.lock();running=true;let token=generation;lock.unlock()
        capture.async {[weak self] in
            guard let self=self else{return}
            let secured=url.startAccessingSecurityScopedResource();defer{if secured{url.stopAccessingSecurityScopedResource()}}
            do {
                let asset=AVURLAsset(url:url)
                guard let track=asset.tracks(withMediaType:.video).first else{throw AppError.message("MP4 has no video track")}
                let reader=try AVAssetReader(asset:asset)
                // Video composition applies the file's preferred orientation.
                let output=AVAssetReaderVideoCompositionOutput(videoTracks:[track],videoSettings:[kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_420YpCbCr8BiPlanarFullRange])
                output.videoComposition=AVVideoComposition(propertiesOf:asset);output.alwaysCopiesSampleData=false
                guard reader.canAdd(output) else{throw AppError.message("Unsupported MP4 decoder")};reader.add(output)
                self.lock.lock();self.reader=reader;self.lock.unlock()
                guard reader.startReading() else{throw reader.error ?? AppError.message("MP4 cannot start")}
                let start=clockNow();var first:Double?
                while let sample=output.copyNextSampleBuffer() {
                    self.lock.lock();let active=self.running&&self.generation==token;self.lock.unlock();if !active{break}
                    let pts=CMSampleBufferGetPresentationTimeStamp(sample).seconds;if first==nil{first=pts}
                    let due=start+pts-first!
                    while clockNow()<due {self.lock.lock();let cancelled = !self.running || self.generation != token;self.lock.unlock();if cancelled{reader.cancelReading();return};Thread.sleep(forTimeInterval:0.002)}
                    if clockNow()-due<0.1,let f=CMSampleBufferGetImageBuffer(sample){self.offer(f,time:clockNow())}
                }
                if reader.status == .failed {throw reader.error ?? AppError.message("MP4 decode failed")}
            }catch{self.onError?(error.localizedDescription)}
        }
    }
    func stop() {
        lock.lock();running=false;generation+=1;pending=nil;reader?.cancelReading();reader=nil;lock.unlock()
        capture.async {[weak self] in if self?.session.isRunning==true{self?.session.stopRunning()}}
    }
}
