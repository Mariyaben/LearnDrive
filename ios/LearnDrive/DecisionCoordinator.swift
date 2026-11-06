import Foundation

struct DeskOptions {
    var mode="Desk"
    var speed=60.0,pitch=0.0,tti=10.0
    var physical=false,braking=false,gear=false
    var requireGps=false
    var manualSpeed=30.0
    var script:DeskScript?
    var scriptStart=0.0
}
// All evaluate/accept operations live on one serial queue. Vision can wake it
// immediately; UI rendering and PCM playback do not run on this queue.
final class DecisionCoordinator {
    private let queue=DispatchQueue(label:"spatial-rules")
    private let core:LDCore
    private let audio:AudioWorker
    private var config=DeskOptions()
    private var timer:DispatchSourceTimer?
    private var result:VisionResult?
    private var running=false
    private var suppressed=false
    var onDisplay:((Telemetry,VisionResult?,String)->Void)?
    var onPrompt:((Int)->Void)?
    init(core:LDCore,audio:AudioWorker){self.core=core;self.audio=audio}
    func update(_ options:DeskOptions){queue.async{if self.config.mode != options.mode {self.core.drivingReset()};self.config=options}}
    func receive(_ result:VisionResult){queue.async{self.result=result;self.evaluate()}}
    func start(){queue.async {
        guard !self.running else{return};self.running=true
        let timer=DispatchSource.makeTimerSource(queue:self.queue)
        timer.schedule(deadline:.now(),repeating:.milliseconds(10));timer.setEventHandler{[weak self] in self?.evaluate()}
        self.timer=timer;timer.resume()
    }}
    func stop(){queue.async{self.running=false;self.result=nil;self.timer?.cancel();self.timer=nil}}
    func clearVision(){queue.async{self.result=nil;self.core.drivingReset()}}
    private func evaluate(){
        guard running else{return}
        let time=clockNow(),measured=Telemetry(values:core.snapshot(time)),row=config.script?.at(time-config.scriptStart)
        let simulated=config.mode != "Camera"
        let t=simulated ? Telemetry.mock(time:time,speed:row?.speed ?? config.speed,pitch:row?.pitch ?? config.pitch,braking:row?.braking ?? config.braking,physical:config.physical ? measured:nil):
            Telemetry(values:core.adviceTelemetry(measured.values,requireGps:config.requireGps,manualSpeed:config.manualSpeed))
        let speedSource=simulated ? "simulated":(measured.values[6].boolValue && measured.speed.isFinite && measured.speed>=0 ? "GPS + IMU":(t.values[6].boolValue ? "manual fallback":"GPS required"))
        let mute=t.braking||t.values[9].boolValue||t.shock
        if mute && !suppressed {audio.cancel()}
        suppressed=mute
        let ht=config.mode=="Desk" ? time:result?.time ?? 0
        let htti=config.mode=="Desk" ? row?.tti ?? config.tti:result?.tti ?? .infinity
        let hc=config.mode=="Desk" ? row?.confidence ?? 0.9:result?.confidence ?? 0
        let path=config.mode=="Desk" ? row?.path ?? true:result?.inPath ?? false
        let tactical=core.decide(t.values,hazardTime:ht,tti:htti,confidence:hc,path:path,ready:audio.ready,gear:false)
        let driving=core.drivingAdvice(t.values,ready:audio.ready,manual:row?.gear ?? config.gear,slope:true)
        let dual=config.mode=="Camera" ? core.dualAdvice(t.values,time:time):[NSNumber(value:0),NSNumber(value:0)]
        let dualId=dual[0].int32Value
        let candidates=dual[1].boolValue ? [tactical,dualId]:[tactical,dualId,driving]
        let id=candidates.max{core.promptPriority($0)<core.promptPriority($1)} ?? 0
        if id>0,core.audioAllowed(id,time:time),audio.submit(Int(id)) {
            core.audioAccepted(id,time:time)
            if id>=2 && id<=4 {core.accepted(time)}
            if id==dualId {core.dualAccepted(id,time:time)}
            core.drivingAccepted(id,time:time);onPrompt?(Int(id))
        }
        onDisplay?(t,result,speedSource)
    }
}
