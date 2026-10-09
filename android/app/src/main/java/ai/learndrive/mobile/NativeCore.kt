package ai.learndrive.mobile

import java.nio.ByteBuffer

data class Telemetry(val time: Double, val speed: Double, val pitch: Double, val acceleration: Double,
    val progress: Double, val calibrated: Boolean, val speedValid: Boolean, val pitchValid: Boolean,
    val braking: Boolean, val hardBraking: Boolean, val shock: Boolean) {
    fun values() = doubleArrayOf(time,speed,pitch,acceleration,progress,
        if(calibrated)1.0 else 0.0,if(speedValid)1.0 else 0.0,if(pitchValid)1.0 else 0.0,
        if(braking)1.0 else 0.0,if(hardBraking)1.0 else 0.0,if(shock)1.0 else 0.0)
    companion object { fun from(v: DoubleArray) = Telemetry(v[0],v[1],v[2],v[3],v[4],v[5]!=0.0,v[6]!=0.0,v[7]!=0.0,v[8]!=0.0,v[9]!=0.0,v[10]!=0.0) }
}
data class PhoneSlope(val valid:Boolean,val degrees:Double,val roll:Double,val gradePercent:Double,val progress:Double) {
    companion object {fun from(v:DoubleArray)=PhoneSlope(v[0]!=0.0,v[1],v[2],v[3],v[4])}
}
class NativeCore : AutoCloseable {
    private var handle = create().also { check(it != 0L) { "Native engine allocation failed" } }
    // A short shared lock protects fusion/rules and the reusable vision state.
    // Inference runs outside this lock so IMU delivery never waits on a model.
    @Synchronized fun reset() = reset(handle)
    @Synchronized fun imu(t: Double, a: DoubleArray, g: DoubleArray, l: DoubleArray, has: Boolean) = imu(handle,t,a,g,l,has)
    @Synchronized fun gps(t: Double,v: Double,accuracy: Double) = gps(handle,t,v,accuracy)
    @Synchronized fun snapshot(t: Double) = Telemetry.from(snapshot(handle,t))
    @Synchronized fun inclination(t:Double)=PhoneSlope.from(inclination(handle,t))
    @Synchronized fun calibrationReason():String=calibrationReason(handle)
    fun adviceTelemetry(measured:Telemetry,requireGps:Boolean,manualSpeed:Double) =
        Telemetry.from(requireNotNull(adviceTelemetry(measured.values(),requireGps,manualSpeed)))
    fun videoTelemetry(measured:Telemetry,speed:Double,pitch:Double)=Telemetry.from(requireNotNull(videoTelemetry(measured.values(),speed,pitch)))
    fun videoSpeedTelemetry(measured:Telemetry,speed:Double)=Telemetry.from(requireNotNull(videoSpeedTelemetry(measured.values(),speed)))
    @Synchronized fun decide(t: Telemetry,ht: Double,tti: Double,conf: Double,path: Boolean,ready: Boolean,gear: Boolean) = decide(handle,t.values(),ht,tti,conf,path,ready,gear)
    @Synchronized fun accepted(t: Double) = accepted(handle,t)
    @Synchronized fun cameraStarted(t:Double)=cameraStarted(handle,t)
    @Synchronized fun cameraAdvice(t:Telemetry,ready:Boolean,manual:Boolean)=cameraAdvice(handle,t.values(),ready,manual)
    @Synchronized fun cameraAccepted(id:Int,t:Double)=cameraAccepted(handle,id,t)
    @Synchronized fun cameraReason():String=cameraReason(handle)
    external fun promptPriority(id:Int):Int
    external fun promptCount():Int
    external fun slopeBand(degrees:Double):Int
    external fun promptMayInterrupt(next:Int,current:Int):Boolean
    @Synchronized fun roadShape():DoubleArray=roadShape(handle)
    external fun promptText(id:Int):String
    @Synchronized fun drivingReset()=drivingReset(handle)
    @Synchronized fun engineSample(t:Double,gear:Int,maxGear:Int,rpm:Double,lower:Double,upper:Double,load:Boolean)=engineSample(handle,t,gear,maxGear,rpm,lower,upper,load)
    @Synchronized fun drivingAdvice(t:Telemetry,ready:Boolean,manual:Boolean,slope:Boolean)=drivingAdvice(handle,t.values(),ready,manual,slope)
    @Synchronized fun drivingAccepted(id:Int,t:Double)=drivingAccepted(handle,id,t)
    @Synchronized fun drivingReason():String=drivingReason(handle)
    @Synchronized fun prepare(y:ByteBuffer,yr:Int,yp:Int,u:ByteBuffer,ur:Int,up:Int,v:ByteBuffer,vr:Int,vp:Int,w:Int,h:Int,rot:Int,size:Int,tensor:ByteBuffer,dtype:Int,scale:Float,zp:Int,full:Boolean,gray:Boolean,layout:Int=0) {
        check(prepare(handle,y.slice(),yr,yp,u.slice(),ur,up,v.slice(),vr,vp,w,h,rot,size,tensor,dtype,scale,zp,full,gray,layout)) { error(handle) }
    }
    @Synchronized fun vision(t:Double,d:FloatArray,c:Int,n:Int,cf:Boolean,normalized:Boolean,road:FloatArray,lane:FloatArray,w:Int,h:Int):DoubleArray =
        vision(handle,t,d,c,n,cf,normalized,road,lane,w,h) ?: throw IllegalArgumentException(error(handle))
    @Synchronized fun boxes():FloatArray=boxes(handle)
    @Synchronized fun reason():String=reason(handle)
    @Synchronized override fun close() { if(handle!=0L)destroy(handle);handle=0 }
    private external fun create():Long
    private external fun destroy(h:Long)
    private external fun reset(h:Long)
    private external fun imu(h:Long,t:Double,a:DoubleArray,g:DoubleArray,l:DoubleArray,has:Boolean)
    private external fun gps(h:Long,t:Double,v:Double,a:Double):Boolean
    private external fun snapshot(h:Long,t:Double):DoubleArray
    private external fun inclination(h:Long,t:Double):DoubleArray
    private external fun calibrationReason(h:Long):String
    private external fun adviceTelemetry(measured:DoubleArray,requireGps:Boolean,manualSpeed:Double):DoubleArray?
    private external fun videoTelemetry(measured:DoubleArray,speed:Double,pitch:Double):DoubleArray?
    private external fun videoSpeedTelemetry(measured:DoubleArray,speed:Double):DoubleArray?
    private external fun decide(h:Long,t:DoubleArray,ht:Double,tti:Double,conf:Double,path:Boolean,ready:Boolean,gear:Boolean):Int
    private external fun accepted(h:Long,t:Double)
    private external fun cameraStarted(h:Long,t:Double)
    private external fun cameraAdvice(h:Long,t:DoubleArray,ready:Boolean,manual:Boolean):Int
    private external fun roadShape(h:Long):DoubleArray
    private external fun cameraAccepted(h:Long,id:Int,t:Double)
    private external fun cameraReason(h:Long):String
    private external fun drivingReset(h:Long)
    private external fun engineSample(h:Long,t:Double,gear:Int,maxGear:Int,rpm:Double,lower:Double,upper:Double,load:Boolean)
    private external fun drivingAdvice(h:Long,t:DoubleArray,ready:Boolean,manual:Boolean,slope:Boolean):Int
    private external fun drivingAccepted(h:Long,id:Int,t:Double)
    private external fun drivingReason(h:Long):String
    private external fun prepare(h:Long,y:ByteBuffer,yr:Int,yp:Int,u:ByteBuffer,ur:Int,up:Int,v:ByteBuffer,vr:Int,vp:Int,w:Int,height:Int,rot:Int,size:Int,tensor:ByteBuffer,dtype:Int,scale:Float,zp:Int,full:Boolean,gray:Boolean,layout:Int):Boolean
    private external fun boxes(h:Long):FloatArray
    private external fun reason(h:Long):String
    private external fun vision(h:Long,t:Double,d:FloatArray,c:Int,n:Int,cf:Boolean,normalized:Boolean,road:FloatArray,lane:FloatArray,w:Int,height:Int):DoubleArray?
    private external fun error(h:Long):String
    companion object { init { System.loadLibrary("learndrive") } }
}
