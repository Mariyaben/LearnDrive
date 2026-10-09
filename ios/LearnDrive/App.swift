import SwiftUI
import AVFoundation
import UniformTypeIdentifiers
import Foundation

@MainActor final class AppModel:ObservableObject {
    @Published var speed=60.0
    @Published var pitch=0.0
    @Published var tti=10.0
    @Published var braking=false
    @Published var physical=false
    @Published var gear=false
    @Published var requireGps=false {didSet{UserDefaults.standard.set(requireGps,forKey:"requireGps")}}
    @Published var manualSpeed=30.0 {didSet{UserDefaults.standard.set(manualSpeed,forKey:"manualAdviceSpeed")}}
    @Published var mode="Desk"
    @Published var status="Warming offline voice"
    @Published var prompt="Desk mode · simulated input"
    @Published var metrics=""
    let core=LDCore()
    lazy var sensors=Sensors(core:core)
    lazy var source=FrameSource(core:core)
    lazy var audio=AudioWorker(core:core)
    lazy var coordinator=DecisionCoordinator(core:core,audio:audio)
    private var timer:Timer?
    private var result:VisionResult?
    private var script:DeskScript?
    private var scriptStart=0.0
    private var active=false
    init() {
        requireGps=UserDefaults.standard.bool(forKey:"requireGps")
        let savedSpeed=(UserDefaults.standard.object(forKey:"manualAdviceSpeed") as? NSNumber)?.doubleValue ?? 30
        manualSpeed=savedSpeed.isFinite ? min(160,max(0,savedSpeed)):30
        let rules=coordinator
        source.onResult={value in rules.receive(value)}
        source.onError={[weak self] message in DispatchQueue.main.async {self?.status=message}}
        audio.onStatus={[weak self] message in DispatchQueue.main.async {self?.status=message}}
        coordinator.onPrompt={[weak self] id in DispatchQueue.main.async {guard let self=self else{return};self.prompt=self.core.promptText(Int32(id))}}
        coordinator.onDisplay={[weak self] t,r,speedSource in DispatchQueue.main.async {
            guard let self=self else{return}
            self.result=r
            let speedText=t.values[6].boolValue ? String(format:"Advice %.1f km/h (%@)",t.speed,speedSource):"Advice speed unavailable (\(speedSource))"
            self.metrics=speedText+String(format:" | pitch %.1f° | TTI %.1f s\nCalibration %d%% | %@ | %@\nVision %.1f ms",t.pitch,self.mode=="Desk" ? self.tti:r?.tti ?? .infinity,Int(t.progress*100),t.braking ? "braking":"coasting",t.shock ? "tap/shock":"quiet",r?.processingMs ?? 0)
        }}
    }
    func resume() {
        guard !active else{return};active=true;sensors.start();tick();coordinator.start()
        timer=Timer.scheduledTimer(withTimeInterval:0.05,repeats:true) {[weak self] _ in Task {@MainActor in self?.tick()}}
        if mode=="Camera"{source.camera()}
    }
    func pause(){active=false;timer?.invalidate();timer=nil;source.stop();sensors.stop();audio.cancel();coordinator.stop();result=nil}
    func select(_ next:String){source.stop();audio.cancel();result=nil;coordinator.clearVision();mode=next;if next=="Camera"{script=nil};tick();prompt="\(next) · \(next == "Camera" ? "live telemetry":"simulated telemetry")";if next=="Camera"{source.camera()}}
    func openVideo(_ url:URL){select("MP4");source.video(url)}
    func openScript(_ url:URL) {
        let secured=url.startAccessingSecurityScopedResource();defer{if secured{url.stopAccessingSecurityScopedResource()}}
        do {script=try DeskScript(text:String(contentsOf:url,encoding:.utf8));scriptStart=clockNow();select("Desk");status="CSV trajectory running"}catch{status=error.localizedDescription}
    }
    func manualDesk(){script=nil;select("Desk")}
    func recalibrate(){audio.cancel();core.reset();coordinator.clearVision();result=nil;status="Rear camera forward; hold mount still 5 seconds"}
    func testAudio(){audio.cancel();_ = audio.submit(1);DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(250)){[weak self] in _ = self?.audio.submit(4)}}
    private func tick() {
        guard active else{return}
        coordinator.update(DeskOptions(mode:mode,speed:speed,pitch:pitch,tti:tti,physical:physical,braking:braking,gear:gear,requireGps:requireGps,manualSpeed:manualSpeed,script:script,scriptStart:scriptStart))
    }
}
struct CameraPreview:UIViewRepresentable {
    let session:AVCaptureSession
    final class View:UIView {
        override class var layerClass:AnyClass {AVCaptureVideoPreviewLayer.self}
        var preview:AVCaptureVideoPreviewLayer {layer as! AVCaptureVideoPreviewLayer}
    }
    func makeUIView(context:Context)->View{let v=View();v.preview.session=session;v.preview.videoGravity = .resizeAspect;v.preview.connection?.videoOrientation = .landscapeRight;return v}
    func updateUIView(_ v:View,context:Context){v.preview.session=session}
}
@MainActor struct ContentView:View {
    @StateObject private var model=AppModel()
    @Environment(\.scenePhase) private var phase
    @State private var videoPicker=false
    @State private var csvPicker=false
    var body:some View {
        VStack(alignment:.leading,spacing:12) {
            Text("LearnDrive.AI / Mobile lab").font(.title2.bold())
            HStack(alignment:.top,spacing:20) {
                VStack(alignment:.leading) {
                    CameraPreview(session:model.source.session).frame(minHeight:120)
                    Text(model.prompt).font(.title3)
                    Text(model.metrics).font(.system(.caption,design:.monospaced))
                    Text(model.status).font(.caption).foregroundStyle(.secondary)
                }.frame(maxWidth:.infinity)
                ScrollView {
                    VStack(alignment:.leading,spacing:12) {
                        HStack {Button("Desk"){model.manualDesk()};Button("Camera"){model.select("Camera")};Button("MP4"){videoPicker=true}}
                        if model.mode == "Camera" {
                            Toggle("Require GPS speed",isOn:$model.requireGps).onChange(of:model.requireGps){_ in model.audio.cancel()}
                            if !model.requireGps {
                                Text("Advice speed without GPS: \(Int(model.manualSpeed)) km/h")
                                Slider(value:$model.manualSpeed,in:0...160)
                                Text("GPS is preferred when available. Without a fix, advice uses this manual speed.").font(.caption)
                            }
                        }
                        if model.mode != "Camera" {
                            Text("Simulated speed: \(Int(model.speed)) km/h");Slider(value:$model.speed,in:0...160)
                            Text("Simulated pitch: \(Int(model.pitch))°");Slider(value:$model.pitch,in:-30...30)
                            Text(String(format:"Simulated TTI: %.1f s",model.tti));Slider(value:$model.tti,in:0.1...20)
                            Toggle("Simulate driver braking",isOn:$model.braking)
                            Toggle("Physical IMU pitch / tap detection",isOn:$model.physical)
                        }
                        Button("Load CSV trajectory"){csvPicker=true}
                        Toggle("Manual vehicle · enable gear guidance",isOn:$model.gear)
                        Text("Mount the phone and calibrate on level ground for slope cues. Without live gear/RPM, gear advice is conditional.").font(.caption)
                        Button("Recalibrate · hold still 5 s"){model.recalibrate()}
                        Button("Test audio preemption"){model.testAudio()}
                    }
                }.frame(maxWidth:.infinity)
            }
        }.padding(20).background(Color(red:0.04,green:0.08,blue:0.12)).preferredColorScheme(.dark)
        .fileImporter(isPresented:$videoPicker,allowedContentTypes:[.movie,.mpeg4Movie]) {result in if case .success(let url)=result{model.openVideo(url)}}
        .fileImporter(isPresented:$csvPicker,allowedContentTypes:[.commaSeparatedText,.plainText]) {result in if case .success(let url)=result{model.openScript(url)}}
        .onAppear{model.resume()}.onDisappear{model.pause()}
        .onChange(of:phase){newValue in if newValue == .active {model.resume()}else{model.pause()}}
    }
}
@main struct LearnDriveApp:App {var body:some Scene{WindowGroup{ContentView()}}}
