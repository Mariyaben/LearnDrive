import Foundation
import CoreMotion
import CoreLocation

func clockNow() -> Double { ProcessInfo.processInfo.systemUptime }
struct Telemetry {
    var values: [NSNumber]
    var speed:Double { values[1].doubleValue }
    var pitch:Double { values[2].doubleValue }
    var progress:Double { values[4].doubleValue }
    var calibrated:Bool { values[5].boolValue }
    var braking:Bool { values[8].boolValue }
    var shock:Bool { values[10].boolValue }
    static func mock(time:Double,speed:Double,pitch:Double,braking:Bool,physical:Telemetry?=nil) -> Telemetry {
        Telemetry(values:[time,speed,physical?.pitch ?? pitch,physical?.values[3].doubleValue ?? 0,physical?.progress ?? 1,
            physical?.calibrated == false ? 0:1,physical?.values[7].doubleValue ?? 1,physical?.values[7].doubleValue ?? 1,
            braking || physical?.braking == true ? 1:0,physical?.values[9].doubleValue ?? 0,physical?.shock == true ? 1:0].map(NSNumber.init(value:)))
    }
}
final class Sensors:NSObject,CLLocationManagerDelegate {
    private let motion=CMMotionManager()
    private let locations=CLLocationManager()
    private let core:LDCore
    private let queue:OperationQueue = {let q=OperationQueue();q.name="imu-100hz";q.maxConcurrentOperationCount=1;return q}()
    init(core:LDCore) {self.core=core;super.init();locations.delegate=self;locations.desiredAccuracy=kCLLocationAccuracyBest;locations.activityType = .automotiveNavigation}
    func start() {
        if !motion.isDeviceMotionActive && motion.isDeviceMotionAvailable {
            motion.deviceMotionUpdateInterval=0.01
            motion.startDeviceMotionUpdates(using:.xArbitraryZVertical,to:queue) {[weak self] data,_ in
                guard let self=self,let d=data else{return}
                // Core Motion gravity points DOWN. Shared core expects proper
                // acceleration +g; reversing gravity makes both platforms agree.
                let a=[d.userAcceleration.x-d.gravity.x,d.userAcceleration.y-d.gravity.y,d.userAcceleration.z-d.gravity.z].map {$0*9.80665}
                let l=[d.userAcceleration.x,d.userAcceleration.y,d.userAcceleration.z].map {$0*9.80665}
                self.core.imu(d.timestamp,acceleration:a.map(NSNumber.init(value:)),gyro:[d.rotationRate.x,d.rotationRate.y,d.rotationRate.z].map(NSNumber.init(value:)),linear:l.map(NSNumber.init(value:)))
            }
        }
        locations.requestWhenInUseAuthorization()
        if locations.authorizationStatus == .authorizedWhenInUse || locations.authorizationStatus == .authorizedAlways {locations.startUpdatingLocation()}
    }
    func locationManagerDidChangeAuthorization(_ manager:CLLocationManager) {
        if manager.authorizationStatus == .authorizedWhenInUse || manager.authorizationStatus == .authorizedAlways {manager.startUpdatingLocation()}
    }
    func locationManager(_ manager:CLLocationManager,didUpdateLocations locations:[CLLocation]) {
        guard let l=locations.last,l.speed>=0,l.speedAccuracy>0 else{return}
        // CLLocation uses wall-clock timestamps: translate age into uptime once.
        let age=Date().timeIntervalSince(l.timestamp)
        guard age>=0,age<2 else{return}
        core.gps(clockNow()-age,speed:l.speed,accuracy:max(0.5,l.speedAccuracy))
    }
    func stop() {motion.stopDeviceMotionUpdates();locations.stopUpdatingLocation()}
}
struct DeskRow {
    var time,speed,pitch,tti,confidence:Double
    var braking,path,gear:Bool
}
struct DeskScript {
    let rows:[DeskRow]
    init(text:String) throws {
        let lines=text.split(whereSeparator:\.isNewline)
        guard lines.first=="time,speed_kmh,pitch_deg,braking,tti_s,confidence,in_path,allow_gear" else {throw AppError.message("Unexpected CSV header")}
        rows=try lines.dropFirst().map {line in
            let pieces=line.split(separator:",",omittingEmptySubsequences:false)
            let v=pieces.compactMap {Double($0)}
            guard v.count==8,v.allSatisfy({$0.isFinite}),v[0]>=0,(0...160).contains(v[1]),(-30...30).contains(v[2]),v[4]>0,(0...1).contains(v[5]) else{throw AppError.message("Invalid CSV row")}
            return DeskRow(time:v[0],speed:v[1],pitch:v[2],tti:v[4],confidence:v[5],braking:v[3] != 0,path:v[6] != 0,gear:v[7] != 0)
        }
        guard !rows.isEmpty,zip(rows,rows.dropFirst()).allSatisfy({pair in pair.0.time<pair.1.time}) else{throw AppError.message("CSV timestamps must increase")}
    }
    func at(_ time:Double)->DeskRow {rows.last(where:{$0.time<=time}) ?? rows[0]}
}
enum AppError:Error,LocalizedError {
    case message(String)
    var errorDescription:String? {if case .message(let text)=self{return text};return nil}
}
