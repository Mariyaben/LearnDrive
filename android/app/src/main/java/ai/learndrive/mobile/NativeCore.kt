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
    @Volatile var sourceId:Long=0; private set
    @Volatile var coachingSettings=CoachingSettings(); private set
    private var attemptedSource=0L
    private var attemptedRevision=0L
    private var attemptedEvent=0L
    private var attemptedAt=Double.NEGATIVE_INFINITY
    @Synchronized fun configureCoaching(settings:CoachingSettings) {
        check(coachingConfigure(handle,settings.profile.nativeId,settings.voiceEnabled,settings.revision)) {"Coaching settings rejected"}
        coachingSettings=settings
    }
    @Synchronized fun beginSource(id:Long,kind:CaptureSource,time:Double) {
        check(guidanceBegin(handle,id,kind.nativeId,time));sourceId=id
    }
    @Synchronized fun requireSource(id:Long) {if(id!=sourceId)throw StaleSourceException()}
    @Synchronized fun guidance(t:Telemetry,now:Double,id:Long,active:Boolean,ready:Boolean,manual:Boolean,
        slope:Boolean,emergencies:Boolean,ht:Double,tti:Double,confidence:Double,path:Boolean):GuidanceDecision =
        GuidanceDecision.from(requireNotNull(guidanceDecide(handle,id,t.values(),now,active,ready,manual,slope,emergencies,ht,tti,confidence,path)),now)
            .copy(settingsRevision=coachingSettings.revision)
    data class CoachingDelivery(val voiceSubmitted:Boolean,val acceptedAt:Double)
    /** Logical acceptance is independent of PCM availability. A submission is
     * recorded only when the worker really accepts a clip; onset revalidates the
     * accepted ticket under the native lock before AudioTrack.play. */
    @Synchronized fun deliverCoaching(d:GuidanceDecision,audio:AudioWorker):CoachingDelivery? {
        val time=now()
        if(d.sourceId!=sourceId||!d.eligible||d.settingsRevision!=coachingSettings.revision||
            time<d.decidedAt||time-d.decidedAt>=.35||!guidancePending(handle,d.sequence,d.prompt,time))return null
        val diagnostic=coachingDiagnostics(handle,time)
        val validatedWarning=diagnostic[4]==2.0
        if(coachingSettings.voiceEnabled) {
            val event=diagnostic[6].toLong()
            if(attemptedSource==sourceId&&attemptedRevision==d.settingsRevision&&attemptedEvent==event&&time-attemptedAt in 0.0..<.25)return null
            attemptedSource=sourceId;attemptedRevision=d.settingsRevision;attemptedEvent=event;attemptedAt=time
        }
        if(coachingSettings.voiceEnabled)coachingDeliveryEvent(handle,d.sequence,d.prompt,time,1)
        val gate=object:AudioPlaybackGate {
          override fun start(play:(()->Boolean)->Boolean):Boolean = synchronized(this@NativeCore) {
                var checkedAt=0.0
                // The worker takes its monitor only after this native monitor.
                // Revalidate inside that boundary: waiting for the audio monitor
                // must not consume the remainder of the evidence lifetime.
                val started=play {
                    checkedAt=now()
                    handle!=0L&&d.sourceId==sourceId&&d.settingsRevision==coachingSettings.revision&&
                        coachingPlaybackValid(handle,d.sequence,d.prompt,checkedAt,d.settingsRevision)
                }
                if(started)coachingPlaybackStarted(handle,d.sequence,d.prompt,checkedAt,d.settingsRevision)
                started
            }
          override fun terminal(stage:Int) = synchronized(this@NativeCore) {
              if(handle!=0L&&d.sourceId==sourceId&&d.settingsRevision==coachingSettings.revision)
                  coachingDeliveryEvent(handle,d.sequence,d.prompt,now(),stage)
          }
        }
        val submitted=coachingSettings.voiceEnabled&&audio.submitCoaching(d.prompt,validatedWarning,gate)
        val acceptedAt=now()
        if(!coachingAccept(handle,d.sequence,d.prompt,acceptedAt,submitted)) {
            if(submitted)audio.cancel()
            return null
        }
        return CoachingDelivery(submitted,acceptedAt)
    }
    @Synchronized fun coachingDiagnostics(time:Double)=coachingDiagnostics(handle,time)
    @Synchronized fun coachingDiagnosticsJson(time:Double)=coachingDiagnosticsJson(handle,time)
    @Synchronized fun temporalContext():String=temporalContext(handle)
    @Synchronized fun guidanceAccepted(d:GuidanceDecision,time:Double)=guidanceAccepted(handle,d.sequence,d.prompt,time)
    @Synchronized fun deliverGuidance(d:GuidanceDecision,submit:(Int)->Boolean,cancel:()->Unit):Boolean {
        val time=now()
        if(d.sourceId!=sourceId||!d.eligible||time<d.decidedAt||time-d.decidedAt>=.35)return false
        if(!guidancePending(handle,d.sequence,d.prompt,time))return false
        if(!submit(d.prompt))return false
        val accepted=guidanceAccepted(handle,d.sequence,d.prompt,now())
        if(!accepted)cancel()
        return accepted
    }
    @Synchronized fun guidanceReason():String=guidanceReason(handle)
    // A short shared lock protects fusion/rules and the reusable vision state.
    // Inference runs outside this lock so IMU delivery never waits on a model.
    @Synchronized fun reset() = reset(handle)
    @Synchronized fun calibrationId():Long=calibrationId(handle)
    @Synchronized fun cameraIntrinsics(focalLengthYPixels:Double)=cameraIntrinsics(handle,focalLengthYPixels)
    @Synchronized fun cameraUp(time:Double)=cameraUp(handle,time)
    @Synchronized fun spatialPublish(id:Long,time:Double,output:ByteBuffer):Boolean {requireSource(id);return spatialPublish(handle,id,time,output)}
    @Synchronized fun spatialUnavailable(id:Long,time:Double) {requireSource(id);check(spatialUnavailable(handle,id,time)){"Spatial invalidation rejected"}}
    @Synchronized fun frameGeometry(id:Long,values:DoubleArray) {requireSource(id);spatialGeometry(handle,values)}
    @Synchronized fun spatialLabels(id:Long,time:Double,values:FloatArray,n:Int,cf:Boolean):DoubleArray {requireSource(id);return spatialLabels(handle,id,time,values,n,cf)?:throw IllegalStateException(error(handle))}
    @Synchronized fun audioAllowed(id:Int,time:Double)=audioAllowed(handle,id,time)
    @Synchronized fun audioAccepted(id:Int,time:Double)=audioAccepted(handle,id,time)
    @Synchronized fun bindAudioDispatcher(dispatcher:ai.learndrive.mobile.audio.AudioDispatcher)=bindAudioDispatcher(handle,dispatcher)
    @Synchronized fun imu(t: Double, a: DoubleArray, g: DoubleArray, l: DoubleArray, has: Boolean) = imu(handle,t,a,g,l,has)
    @Synchronized fun gps(t: Double,v: Double,accuracy: Double) = gps(handle,t,v,accuracy)
    @Synchronized fun navigationRoute(id:Long,shape:DoubleArray,turns:IntArray)=navigationRoute(handle,id,shape,turns)
    @Synchronized fun navigationClear()=navigationClear(handle)
    @Synchronized fun navigationTurnUnavailable()=navigationTurnUnavailable(handle)
    /** Owned normalized maneuver/state IDs, never provider SDK integer values.
     * Unknown step=-1; unknown estimates=POSITIVE_INFINITY. Receipt time is
     * elapsedRealtime seconds; GPS freshness is checked independently. */
    @Synchronized fun navigationTurn(provider:Int,session:Long,routeEpoch:Long,sequence:Long,receivedAt:Double,
        state:Int,stepNumber:Int,maneuver:Int,remainingMeters:Double,remainingSeconds:Double):Boolean =
        navigationTurn(handle,provider,session,routeEpoch,sequence,receivedAt,state,stepNumber,maneuver,remainingMeters,remainingSeconds)
    @Synchronized fun navigationFix(t:Double,lat:Double,lon:Double,accuracy:Double,speed:Double,speedAccuracy:Double,speedValid:Boolean,bearing:Double,bearingValid:Boolean)=
        navigationFix(handle,sourceId,t,lat,lon,accuracy,speed,speedAccuracy,speedValid,bearing,bearingValid)
    @Synchronized fun navigationContext(t:Double)=navigationContext(handle,t)
    @Synchronized fun navigationStatus(t:Double)=navigationStatus(handle,t)
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
    external fun benchmark():DoubleArray
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
    @Synchronized fun dualPolicyAdvice(t:Telemetry,now:Double):IntArray=dualPolicyAdvice(handle,t.values(),now)?:intArrayOf(0,0)
    @Synchronized fun dualPolicyAccepted(id:Int,t:Double)=dualPolicyAccepted(handle,id,t)
    @Synchronized fun dualPolicyReset()=dualPolicyReset(handle)
    @Synchronized fun dualPolicyReason():String=dualPolicyReason(handle)
    @Synchronized fun prepareFrame(id:Long,y:ByteBuffer,yr:Int,yp:Int,u:ByteBuffer,ur:Int,up:Int,v:ByteBuffer,vr:Int,vp:Int,w:Int,h:Int,rot:Int,size:Int,tensor:ByteBuffer,dtype:Int,scale:Float,zp:Int,full:Boolean,gray:Boolean,layout:Int=0) {
        requireSource(id)
        check(prepare(handle,y.slice(),yr,yp,u.slice(),ur,up,v.slice(),vr,vp,w,h,rot,size,tensor,dtype,scale,zp,full,gray,layout)) { error(handle) }
    }
    @Synchronized fun vision(t:Double,d:FloatArray,c:Int,n:Int,cf:Boolean,normalized:Boolean,road:FloatArray,lane:FloatArray,w:Int,h:Int):DoubleArray =
        vision(handle,t,d,c,n,cf,normalized,road,lane,w,h) ?: throw IllegalArgumentException(error(handle))
    @Synchronized fun boxes():FloatArray=boxes(handle)
    @Synchronized fun reason():String=reason(handle)
    @Synchronized override fun close() { if(handle!=0L)destroy(handle);handle=0;sourceId=0 }
    private external fun create():Long
    private external fun coachingConfigure(h:Long,profile:Int,voice:Boolean,revision:Long):Boolean
    private external fun coachingAccept(h:Long,sequence:Long,prompt:Int,time:Double,voiceSubmitted:Boolean):Boolean
    private external fun coachingPlaybackValid(h:Long,sequence:Long,prompt:Int,time:Double,revision:Long):Boolean
    private external fun coachingPlaybackStarted(h:Long,sequence:Long,prompt:Int,time:Double,revision:Long)
    private external fun coachingDiagnostics(h:Long,time:Double):DoubleArray
    private external fun coachingDiagnosticsJson(h:Long,time:Double):String
    private external fun temporalContext(h:Long):String
    private external fun coachingDeliveryEvent(h:Long,sequence:Long,prompt:Int,time:Double,stage:Int)
    private external fun guidanceBegin(h:Long,id:Long,kind:Int,time:Double):Boolean
    private external fun guidanceDecide(h:Long,id:Long,t:DoubleArray,now:Double,active:Boolean,ready:Boolean,manual:Boolean,slope:Boolean,emergencies:Boolean,ht:Double,tti:Double,confidence:Double,path:Boolean):DoubleArray?
    private external fun guidanceAccepted(h:Long,sequence:Long,prompt:Int,time:Double):Boolean
    private external fun guidancePending(h:Long,sequence:Long,prompt:Int,time:Double):Boolean
    private external fun guidanceReason(h:Long):String
    private external fun destroy(h:Long)
    private external fun reset(h:Long)
    private external fun calibrationId(h:Long):Long
    private external fun cameraIntrinsics(h:Long,fy:Double)
    private external fun cameraUp(h:Long,time:Double):DoubleArray
    private external fun spatialPublish(h:Long,id:Long,time:Double,output:ByteBuffer):Boolean
    private external fun spatialUnavailable(h:Long,id:Long,time:Double):Boolean
    private external fun spatialGeometry(h:Long,values:DoubleArray)
    private external fun spatialLabels(h:Long,id:Long,time:Double,values:FloatArray,n:Int,cf:Boolean):DoubleArray?
    private external fun audioAllowed(h:Long,id:Int,time:Double):Boolean
    private external fun audioAccepted(h:Long,id:Int,time:Double)
    private external fun bindAudioDispatcher(h:Long,dispatcher:ai.learndrive.mobile.audio.AudioDispatcher)
    private external fun imu(h:Long,t:Double,a:DoubleArray,g:DoubleArray,l:DoubleArray,has:Boolean)
    private external fun gps(h:Long,t:Double,v:Double,a:Double):Boolean
    private external fun navigationRoute(h:Long,id:Long,shape:DoubleArray,turns:IntArray):Boolean
    private external fun navigationClear(h:Long)
    private external fun navigationTurnUnavailable(h:Long)
    private external fun navigationTurn(h:Long,provider:Int,session:Long,routeEpoch:Long,sequence:Long,receivedAt:Double,
        state:Int,stepNumber:Int,maneuver:Int,remainingMeters:Double,remainingSeconds:Double):Boolean
    private external fun navigationFix(h:Long,source:Long,t:Double,lat:Double,lon:Double,accuracy:Double,speed:Double,speedAccuracy:Double,speedValid:Boolean,bearing:Double,bearingValid:Boolean):Boolean
    private external fun navigationContext(h:Long,t:Double):DoubleArray
    private external fun navigationStatus(h:Long,t:Double):String
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
    private external fun dualPolicyAdvice(h:Long,t:DoubleArray,now:Double):IntArray?
    private external fun dualPolicyAccepted(h:Long,prompt:Int,time:Double)
    private external fun dualPolicyReset(h:Long)
    private external fun dualPolicyReason(h:Long):String
    private external fun prepare(h:Long,y:ByteBuffer,yr:Int,yp:Int,u:ByteBuffer,ur:Int,up:Int,v:ByteBuffer,vr:Int,vp:Int,w:Int,height:Int,rot:Int,size:Int,tensor:ByteBuffer,dtype:Int,scale:Float,zp:Int,full:Boolean,gray:Boolean,layout:Int):Boolean
    private external fun boxes(h:Long):FloatArray
    private external fun reason(h:Long):String
    private external fun vision(h:Long,t:Double,d:FloatArray,c:Int,n:Int,cf:Boolean,normalized:Boolean,road:FloatArray,lane:FloatArray,w:Int,height:Int):DoubleArray?
    private external fun error(h:Long):String
    companion object { init { System.loadLibrary("learndrive") } }
}
