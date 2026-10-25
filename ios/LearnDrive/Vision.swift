import Foundation
import AVFoundation
import CoreML
import simd

struct VisionResult {let time,tti,confidence,processingMs:Double;let inPath:Bool}
final class Vision {
    private let core:LDCore
    init(core:LDCore) throws {
        self.core=core
        guard let spatial=Bundle.main.path(forResource:"supercombo",ofType:"onnx") else{throw AppError.message("Bundled Supercombo model missing")}
        guard core.startSpatial(spatial) else{throw AppError.message(core.error())}
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
    func process(_ frame:CVPixelBuffer,time:Double,intrinsics:[NSNumber])throws->VisionResult {
        let start=clockNow()
        guard let lead=core.spatialFrame(frame,time:time,intrinsics:intrinsics) else{throw AppError.message(core.error())}
        let measured=core.snapshot(time),closing=measured[1].doubleValue/3.6-lead[2].doubleValue
        let tti=measured[6].boolValue && closing>0.1 ? lead[0].doubleValue/closing:Double.infinity
        return VisionResult(time:time,tti:tti,confidence:lead[1].doubleValue,processingMs:(clockNow()-start)*1000,inPath:lead[1].doubleValue>=0.65)
    }
}
final class FrameSource:NSObject,AVCaptureVideoDataOutputSampleBufferDelegate {
    let session=AVCaptureSession()
    private let capture=DispatchQueue(label:"camera-capture")
    private let inference=DispatchQueue(label:"vision")
    private let lock=NSLock()
    private var pending:(CVPixelBuffer,Double,[NSNumber])?
    private var fallbackFov=0.0
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
    private func offer(_ frame:CVPixelBuffer,time:Double,intrinsics:[NSNumber]=[]) {
        lock.lock();guard running else{lock.unlock();return};pending=(frame,time,intrinsics)
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
                    let result=try vision!.process(item.0,time:item.1,intrinsics:item.2)
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
                    self.fallbackFov=Double(device.activeFormat.videoFieldOfView)
                    let input=try AVCaptureDeviceInput(device:device)
                    guard self.session.canAddInput(input) else{throw AppError.message("Cannot attach camera")};self.session.addInput(input)
                    let output=AVCaptureVideoDataOutput();output.alwaysDiscardsLateVideoFrames=true
                    output.videoSettings=[kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_420YpCbCr8BiPlanarFullRange]
                    output.setSampleBufferDelegate(self,queue:self.capture)
                    guard self.session.canAddOutput(output) else{throw AppError.message("Cannot attach analyzer")};self.session.addOutput(output)
                    output.connection(with:.video)?.videoOrientation = .landscapeRight
                    if let connection=output.connection(with:.video),connection.isCameraIntrinsicMatrixDeliverySupported {
                        connection.isCameraIntrinsicMatrixDeliveryEnabled=true
                    }
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
        let width=Double(CVPixelBufferGetWidth(f)),height=Double(CVPixelBufferGetHeight(f))
        var intrinsics:[NSNumber]=[]
        if let data=CMGetAttachment(sampleBuffer,key:kCMSampleBufferAttachmentKey_CameraIntrinsicMatrix,attachmentModeOut:nil) as? Data,
           data.count>=MemoryLayout<simd_float3x3>.size {
            let k=data.withUnsafeBytes{$0.loadUnaligned(as:simd_float3x3.self)}
            intrinsics=[NSNumber(value:k.columns.0.x),NSNumber(value:k.columns.1.y),NSNumber(value:k.columns.2.x),NSNumber(value:k.columns.2.y),NSNumber(value:width),NSNumber(value:height)]
        } else if fallbackFov>0 {
            let focal=width/(2*tan(fallbackFov * .pi/360))
            intrinsics=[focal,focal,width/2,height/2,width,height].map{NSNumber(value:$0)}
        }
        lastFrame=t;offer(f,time:captured,intrinsics:intrinsics)
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
                    if clockNow()-due<0.1,let f=CMSampleBufferGetImageBuffer(sample) {
                        let w=Double(CVPixelBufferGetWidth(f)),h=Double(CVPixelBufferGetHeight(f))
                        let fov=self.fallbackFov>0 ? self.fallbackFov:Double(AVCaptureDevice.default(.builtInWideAngleCamera,for:.video,position:.back)?.activeFormat.videoFieldOfView ?? 60)
                        let focal=w/(2*tan(fov * .pi/360))
                        self.offer(f,time:clockNow(),intrinsics:[focal,focal,w/2,h/2,w,h].map{NSNumber(value:$0)})
                    }
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
