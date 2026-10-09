package ai.learndrive.mobile

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.PowerManager
import android.media.AudioManager
import android.view.Gravity
import android.view.View
import android.util.Log
import android.widget.*
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.core.UseCaseGroup
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

class MainActivity : ComponentActivity() {
    private lateinit var core:NativeCore
    private lateinit var sensors:Sensors
    private lateinit var audio:AudioWorker
    private lateinit var preview:PreviewView
    private lateinit var overlay:DetectionOverlay
    private lateinit var status:TextView
    private lateinit var metrics:TextView
    private lateinit var prompt:TextView
    private lateinit var physical:Switch
    private lateinit var braking:Switch
    private lateinit var gear:Switch
    private lateinit var slope:Switch
    private lateinit var engineLoad:Switch
    private lateinit var filmedVideo:Switch
    private lateinit var phoneSlope:Switch
    private lateinit var planeStatus:TextView
    private lateinit var gpsRequired:Switch
    private lateinit var cameraAdviceControls:LinearLayout
    private lateinit var deskControls:LinearLayout
    private val main=Handler(Looper.getMainLooper())
    private val inference=Executors.newSingleThreadExecutor {r->Thread(r,"vision")}
    private val generation=AtomicInteger()
    private val ruleThread=HandlerThread("spatial-rules").also {it.start()}
    private val rules=Handler(ruleThread.looper)
    private data class Options(val mode:String="Camera",val speed:Double=60.0,val pitch:Double=0.0,val tti:Double=10.0,
        val physical:Boolean=false,val braking:Boolean=false,val gear:Boolean=true,val script:DeskScript?=null,val scriptStart:Double=0.0,
        val requireGps:Boolean=false,val manualSpeed:Double=30.0,val slope:Boolean=true,val demoGear:Int=3,val demoRpm:Double=2000.0,val engineLoad:Boolean=false,
        val filmedVideo:Boolean=true,val videoPitch:Double=0.0,val phoneSlope:Boolean=true)
    @Volatile private var options=Options()
    private data class AdviceDisplay(val telemetry:Telemetry,val speedSource:String)
    @Volatile private var currentAdvice:AdviceDisplay?=null
    private var suppressed=false // Only accessed on the serial rules thread.
    private var camera:ProcessCameraProvider?=null
    private var player:Mp4Source?=null
    private var vision:Vision?=null // Only accessed on the inference executor.
    private var visionFailure:Exception?=null
    @Volatile private var visionError:String?=null
    private var cameraError:String?=null
    @Volatile private var cameraThermal=0
    @Volatile private var lastCameraFrameAt=0.0
    @Volatile private var analysisBusy=false
    private var cameraStartedAt=0.0
    @Volatile private var lastVision:VisionResult?=null
    @Volatile private var resumed=false
    @Volatile private var cameraRunning=false
    private var mode="Camera"
    private var speed=60.0
    private var manualSpeed=30.0
    private var demoGear=3
    private var demoRpm=2000.0
    private var videoPitch=0.0
    private var pitch=0.0
    private var tti=10.0
    private var script:DeskScript?=null
    private var scriptStart=0.0
    private var lastRender=0.0
    @Volatile private var audioStatus="Loading bundled voice"
    private var visionStatus="Starting camera and bundled models"
    @Volatile private var warningStatus="Hold the phone still for calibration"
    private val pickVideo=registerForActivityResult(ActivityResultContracts.OpenDocument()) {uri->if(uri!=null)startVideo(uri)}
    private val pickScript=registerForActivityResult(ActivityResultContracts.OpenDocument()) {uri->
        if(uri!=null)try {
            val text=contentResolver.openInputStream(uri)?.bufferedReader()?.use {it.readText()}?:error("Cannot open CSV")
            script=DeskScript(text);scriptStart=now();switchMode("Desk");status.text="CSV trajectory running"
        }catch(e:Exception){status.text="CSV: ${e.message}"}
    }
    private val permissions=registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        sensors.start();if(mode=="Camera")startCamera()
    }
    private fun text(value:String,size:Float=16f)=TextView(this).apply {text=value;textSize=size;setTextColor(Color.rgb(222,234,243));setPadding(8,6,8,6)}
    private fun button(value:String,action:()->Unit)=Button(this).apply {text=value;setOnClickListener {action()} }
    override fun onCreate(saved:Bundle?) {
        super.onCreate(saved);volumeControlStream=AudioManager.STREAM_MUSIC;core=NativeCore();sensors=Sensors(this,core)
        val outer=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL;setPadding(18,12,18,12);setBackgroundColor(Color.rgb(11,20,29))}
        outer.addView(text("LearnDrive.AI  /  Mobile lab",24f))
        val body=LinearLayout(this);outer.addView(body,LinearLayout.LayoutParams(-1,0,1f))
        val left=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL};body.addView(left,LinearLayout.LayoutParams(0,-1,1f))
        preview=PreviewView(this).apply {scaleType=PreviewView.ScaleType.FIT_CENTER;implementationMode=PreviewView.ImplementationMode.COMPATIBLE}
        overlay=DetectionOverlay(this)
        val cameraFrame=FrameLayout(this).apply {addView(preview,FrameLayout.LayoutParams(-1,-1));addView(this@MainActivity.overlay,FrameLayout.LayoutParams(-1,-1))}
        left.addView(cameraFrame,LinearLayout.LayoutParams(-1,0,1f))
        prompt=text("Camera · looking for people and vehicles",20f);left.addView(prompt)
        metrics=text("");left.addView(metrics)
        status=text("Starting offline camera and voice",13f);left.addView(status)
        val scroll=ScrollView(this);body.addView(scroll,LinearLayout.LayoutParams(0,-1,1f))
        val controls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL};scroll.addView(controls)
        val sources=LinearLayout(this);controls.addView(sources)
        sources.addView(button("Desk") {script=null;switchMode("Desk")})
        sources.addView(button("Camera") {switchMode("Camera");requestPermissions()})
        sources.addView(button("MP4") {pickVideo.launch(arrayOf("video/*"))})
        controls.addView(button("Restart detection") {restartDetection()})
        controls.addView(button("Test voice") {
            audio.cancel()
            if(audio.submit(2))prompt.text="Voice test: Ease off accelerator"
            else status.text="Voice test: $audioStatus · raise media volume"
        })
        val preferences=getSharedPreferences("driving_advice",MODE_PRIVATE)
        manualSpeed=preferences.getInt("manualSpeed",30).coerceIn(0,160).toDouble()
        gear=Switch(this).apply {text="Manual vehicle · gear guidance";isChecked=preferences.getBoolean("manualVehicle",true)};controls.addView(gear)
        gear.setOnCheckedChangeListener {_,enabled->preferences.edit().putBoolean("manualVehicle",enabled).apply()}
        slope=Switch(this).apply {text="Slope guidance";isChecked=preferences.getBoolean("slopeGuidance",true)};controls.addView(slope)
        slope.setOnCheckedChangeListener {_,enabled->preferences.edit().putBoolean("slopeGuidance",enabled).apply()}
        cameraAdviceControls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL}
        controls.addView(cameraAdviceControls)
        filmedVideo=Switch(this).apply {text="Camera pointed at a road video (test)";isChecked=preferences.getBoolean("filmedVideo",true)}
        cameraAdviceControls.addView(filmedVideo)
        phoneSlope=Switch(this).apply {text="Use normalized phone slope";isChecked=preferences.getBoolean("phoneSlope",true)}
        cameraAdviceControls.addView(phoneSlope)
        planeStatus=text("Normalize the mounted phone on level ground",14f);cameraAdviceControls.addView(planeStatus)
        cameraAdviceControls.addView(button("Normalize phone plane · level ground · hold still 5 s") {
            audio.cancel();core.reset();lastVision=null
            planeStatus.text="Normalizing phone plane; hold still on level ground"
        })
        val videoControls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL}
        cameraAdviceControls.addView(videoControls)
        videoControls.addView(text("Choose a simulated slope for video tests. With phone slope ON, the measured phone tilt is used instead.",13f))
        val testSlopes=doubleArrayOf(0.0,3.0,6.0,10.0,15.0,-3.0,-6.0,-10.0,-15.0)
        val videoSlope=Spinner(this).apply {adapter=ArrayAdapter(this@MainActivity,android.R.layout.simple_spinner_dropdown_item,listOf(
            "Video slope: level (test)","Gentle uphill +3° (test)","Uphill +6° (test)","Steep uphill +10° (test)","Very steep uphill +15° (test)",
            "Gentle downhill -3° (test)","Downhill -6° (test)","Steep downhill -10° (test)","Very steep downhill -15° (test)"))}
        videoSlope.onItemSelectedListener=object:AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent:AdapterView<*>?,view:View?,position:Int,id:Long) {videoPitch=testSlopes.getOrElse(position){0.0}}
            override fun onNothingSelected(parent:AdapterView<*>?)=Unit
        }
        videoControls.addView(videoSlope);videoControls.visibility=if(filmedVideo.isChecked&&!phoneSlope.isChecked)View.VISIBLE else View.GONE
        phoneSlope.visibility=if(filmedVideo.isChecked)View.VISIBLE else View.GONE
        phoneSlope.setOnCheckedChangeListener {_,enabled ->
            preferences.edit().putBoolean("phoneSlope",enabled).apply()
            videoControls.visibility=if(filmedVideo.isChecked&&!enabled)View.VISIBLE else View.GONE
            if(::audio.isInitialized){audio.cancel();publishOptions();rules.post {core.drivingReset()}}
        }
        gpsRequired=Switch(this).apply {text="Require GPS speed";isChecked=preferences.getBoolean("requireGps",false)}
        cameraAdviceControls.addView(gpsRequired)
        val gpsButton=button("Enable GPS speed (optional)") {requestPermissions(includeLocation=true)};cameraAdviceControls.addView(gpsButton)
        val fallbackControls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL}
        cameraAdviceControls.addView(fallbackControls)
        slider(fallbackControls,"Video / fallback driving speed",160,manualSpeed.toInt(),"km/h") {
            manualSpeed=it;preferences.edit().putInt("manualSpeed",it.toInt()).apply()
        }
        fallbackControls.addView(text("In video test, this is the driving speed in the video. With video test off, GPS is preferred and this setting is used without a fix.",13f))
        fallbackControls.visibility=if(gpsRequired.isChecked&&!filmedVideo.isChecked)View.GONE else View.VISIBLE
        gpsRequired.isEnabled=!filmedVideo.isChecked;gpsButton.isEnabled=!filmedVideo.isChecked
        filmedVideo.setOnCheckedChangeListener {_,testing ->
            preferences.edit().putBoolean("filmedVideo",testing).apply()
            videoControls.visibility=if(testing&&!phoneSlope.isChecked)View.VISIBLE else View.GONE
            phoneSlope.visibility=if(testing)View.VISIBLE else View.GONE
            gpsRequired.isEnabled=!testing;gpsButton.isEnabled=!testing
            fallbackControls.visibility=if(gpsRequired.isChecked&&!testing)View.GONE else View.VISIBLE
            if(::audio.isInitialized){audio.cancel();publishOptions();rules.post {core.drivingReset()}}
        }
        deskControls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL};controls.addView(deskControls)
        slider(deskControls,"Simulated speed",160,60,"km/h") {speed=it;script=null}
        slider(deskControls,"Simulated pitch",60,30,"°",-30) {pitch=it;script=null}
        slider(deskControls,"Simulated TTI",200,100,"s",0,.1) {tti=it.coerceAtLeast(.1);script=null}
        braking=Switch(this).apply {text="Simulate driver braking"};deskControls.addView(braking)
        physical=Switch(this).apply {text="Use physical IMU pitch / tap detection"};deskControls.addView(physical)
        slider(deskControls,"Simulated current gear",5,2,"",1) {demoGear=it.toInt()}
        slider(deskControls,"Simulated engine revs",60,20,"RPM",0,100.0) {demoRpm=it}
        engineLoad=Switch(this).apply {text="Simulate engine under load"};deskControls.addView(engineLoad)
        deskControls.addView(text("Gear/RPM controls are a Desk simulation. Demo shift band: 1200–3000 RPM.",13f))
        controls.addView(button("Load CSV trajectory") {pickScript.launch(arrayOf("text/*","application/octet-stream"))})
        controls.addView(text("Phone slope estimates the current road grade only with a fixed, forward-facing vehicle mount and level-ground normalization. When filming a road video, it measures your phone's tilt. Set video speed separately. Gear advice is conditional without live gear/RPM. The yellow line shows detected road shape.",13f))
        deskControls.addView(button("Normalize phone plane · level ground · hold still 5 s") {audio.cancel();core.reset();lastVision=null;status.text="Rear camera forward; keep mount stationary on level ground"})
        controls.addView(button("Test audio preemption") {audio.cancel();audio.submit(1);main.postDelayed({audio.submit(4)},250)})
        controls.addView(text("Camera speaks when it starts, then gives guidance for visible road users and large obstacles in the path ahead. This guidance works without calibration, GPS or TTI. Motion evidence controls imminent-collision warnings. Use Test voice to check sound.",13f))
        gpsRequired.setOnCheckedChangeListener {_,required ->
            fallbackControls.visibility=if(required&&!filmedVideo.isChecked)View.GONE else View.VISIBLE
            preferences.edit().putBoolean("requireGps",required).apply()
            if(::audio.isInitialized)audio.cancel()
            publishOptions()
            if(required)requestPermissions(includeLocation=true)
        }
        deskControls.visibility=View.GONE
        setContentView(outer)
        audio=AudioWorker(this,core::promptMayInterrupt,core.promptCount()) {message->main.post {audioStatus=message}}
        requestPermissions()
    }
    private fun slider(parent:LinearLayout,title:String,max:Int,initial:Int,unit:String,offset:Int=0,scale:Double=1.0,change:(Double)->Unit) {
        val label=text("");parent.addView(label)
        val bar=SeekBar(this).apply {this.max=max;progress=initial}
        fun update(value:Int){val mapped=(value+offset)*scale;label.text="$title: %.1f $unit".format(mapped);change(mapped)}
        bar.setOnSeekBarChangeListener(object:SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(b:SeekBar?,p:Int,user:Boolean){update(p)}
            override fun onStartTrackingTouch(b:SeekBar?)=Unit
            override fun onStopTrackingTouch(b:SeekBar?)=Unit
        });parent.addView(bar);update(initial)
    }
    private fun requestPermissions(includeLocation:Boolean=gpsRequired.isChecked&&!filmedVideo.isChecked) {
        val requested=mutableListOf(Manifest.permission.CAMERA)
        if(includeLocation)requested.addAll(listOf(Manifest.permission.ACCESS_FINE_LOCATION,Manifest.permission.ACCESS_COARSE_LOCATION))
        val wanted=requested.filter {ContextCompat.checkSelfPermission(this,it)!=PackageManager.PERMISSION_GRANTED}
        if(wanted.isNotEmpty())permissions.launch(wanted.toTypedArray()) else {sensors.start();if(mode=="Camera")startCamera()}
    }
    private fun publishOptions() {
        options=Options(mode,speed,pitch,tti,physical.isChecked,braking.isChecked,gear.isChecked,script,scriptStart,gpsRequired.isChecked,manualSpeed,slope.isChecked,demoGear,demoRpm,engineLoad.isChecked,filmedVideo.isChecked,videoPitch,phoneSlope.isChecked)
    }
    private fun switchMode(next:String) {
        generation.incrementAndGet();cameraRunning=false;player?.close();player=null;camera?.unbindAll();audio.cancel();lastVision=null;overlay.result=null
        visionError=null;cameraError=null;lastCameraFrameAt=0.0;cameraThermal=0
        mode=next;if(next=="Camera")script=null
        deskControls.visibility=if(next=="Camera")View.GONE else View.VISIBLE
        cameraAdviceControls.visibility=if(next=="Camera")View.VISIBLE else View.GONE
        publishOptions()
        rules.post {core.drivingReset()}
        prompt.text=if(next=="Desk")"Desk mode · simulated input" else "$next · waiting for vision"
        visionStatus=if(next=="Desk")"Desk: simulated inputs; lower TTI to 2 s for a warning" else "Loading camera / video"
        // Preserve the global voice cooldown across source switches.
    }
    private fun model():Vision {
        visionFailure?.let {throw it}
        return vision?:try {Vision(this,core).also {vision=it}}
            catch(e:Exception){visionFailure=e;throw e}
    }
    private fun restartDetection() {
        switchMode("Camera");val token=generation.get()
        visionStatus="Restarting camera and offline detection"
        inference.execute {
            runCatching {vision?.close()}.onFailure {Log.w("LearnDriveVision","Closing models during restart",it)}
            vision=null;visionFailure=null;lastRender=0.0
            main.post {if(resumed&&token==generation.get())requestPermissions()}
        }
    }
    private fun startCamera() {
        if(ContextCompat.checkSelfPermission(this,Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED){cameraRunning=false;cameraError="Camera permission required in Android settings";return}
        visionError=null;cameraError=null;lastCameraFrameAt=0.0;cameraStartedAt=now()
        val token=generation.incrementAndGet();val future=ProcessCameraProvider.getInstance(this)
        future.addListener({
            if(token!=generation.get()||!resumed)return@addListener
            try {
                camera=future.get();camera?.unbindAll()
                val view=Preview.Builder().build().also {it.surfaceProvider=preview.surfaceProvider}
                val analysis=ImageAnalysis.Builder().setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST).build()
                analysis.setAnalyzer(inference) {image->
                    try {
                        if(token==generation.get()&&resumed) {
                            lastCameraFrameAt=now()
                            val thermal=if(android.os.Build.VERSION.SDK_INT>=29)getSystemService(PowerManager::class.java).currentThermalStatus else 0
                            cameraThermal=thermal
                            if(CameraVisionHealth.shouldAnalyze(now(),lastRender,thermal)) {
                                analysisBusy=true;lastRender=now();val result=model().frame(image)
                                if(token==generation.get()){visionError=null;lastVision=result;rules.post {evaluate()}}
                            }
                        }
                    }catch(e:Exception){
                        if(token==generation.get()) {
                            val message=e.message?:e.javaClass.simpleName
                            if(visionError!=message)Log.e("LearnDriveVision","Camera inference failed",e)
                            visionError=message
                        }
                    }
                    finally {analysisBusy=false;image.close()}
                }
                val group=UseCaseGroup.Builder().addUseCase(view).addUseCase(analysis)
                preview.viewPort?.let {group.setViewPort(it)}
                val bound=checkNotNull(camera?.bindToLifecycle(this,CameraSelector.DEFAULT_BACK_CAMERA,group.build()))
                if(!cameraRunning){core.cameraStarted(now());lastVision=null}
                cameraRunning=true
                bound.cameraInfo.cameraState.removeObservers(this)
                bound.cameraInfo.cameraState.observe(this) {state->
                    if(token==generation.get()) {
                        cameraError=state.error?.let {"Camera error ${it.code}: ${it.cause?.message?:"camera stream unavailable"}"}
                        cameraRunning=state.type==androidx.camera.core.CameraState.Type.OPEN
                        rules.post {evaluate()}
                    }
                }
                rules.post {evaluate()}
            }catch(e:Exception){cameraError=e.message?:e.javaClass.simpleName;Log.e("LearnDriveVision","Camera start failed",e)}
        },ContextCompat.getMainExecutor(this))
    }
    private fun startVideo(uri:android.net.Uri) {
        switchMode("MP4");val token=generation.get();val source=Mp4Source();player=source
        inference.execute {try {source.play(this,uri,model()) {if(token==generation.get()){lastVision=it;rules.post {evaluate()}}}}
            catch(e:Exception){main.post {visionStatus="MP4: ${e.message}"}}}
    }
    private fun evaluate() {
            if(!resumed)return
            val config=options
            val t=now();val measured=core.snapshot(t);val row=config.script?.at(t-config.scriptStart)
            val mock=config.mode!="Camera"
            val usePhysical=mock&&config.physical
            val telemetry=if(mock)Telemetry(t,row?.speed?:config.speed,if(usePhysical)measured.pitch else row?.pitch?:config.pitch,measured.acceleration,measured.progress,
                !usePhysical||measured.calibrated,!usePhysical||measured.pitchValid,!usePhysical||measured.pitchValid,(row?.braking?:config.braking)||(usePhysical&&measured.braking),usePhysical&&measured.hardBraking,usePhysical&&measured.shock)
                else if(config.filmedVideo&&config.phoneSlope)core.videoSpeedTelemetry(measured,config.manualSpeed)
                else if(config.filmedVideo)core.videoTelemetry(measured,config.manualSpeed,config.videoPitch)
                else core.adviceTelemetry(measured,config.requireGps,config.manualSpeed)
            val speedSource=when {
                mock->"simulated"
                config.filmedVideo->"video test setting"
                measured.speedValid&&measured.speed.isFinite()&&measured.speed>=0->"GPS + IMU"
                telemetry.speedValid->"manual fallback"
                else->"GPS required"
            }
            currentAdvice=AdviceDisplay(telemetry,speedSource)
            val mute=telemetry.braking||telemetry.hardBraking||telemetry.shock
            if(mute&&!suppressed)audio.cancel()
            suppressed=mute
            val result=lastVision
            val htime=if(config.mode=="Desk")t else result?.time?:0.0
            val htti=if(config.mode=="Desk")row?.tti?:config.tti else result?.tti?:Double.POSITIVE_INFINITY
            val confidence=if(config.mode=="Desk")row?.confidence?:.9 else result?.confidence?:0.0
            val path=if(config.mode=="Desk")row?.path?:true else result?.inPath?:false
            val tactical=core.decide(telemetry,htime,htti,confidence,path,audio.ready,false)
            val cameraId=if(config.mode=="Camera"&&cameraRunning)core.cameraAdvice(telemetry,audio.ready,config.gear) else 0
            if(config.mode=="Desk"&&config.script==null)core.engineSample(t,config.demoGear,6,config.demoRpm,1200.0,3000.0,config.engineLoad)
            val driving=if(config.mode!="Camera"||cameraRunning)core.drivingAdvice(telemetry,audio.ready,row?.gear?:config.gear,config.slope) else 0
            val id=listOf(tactical,driving,cameraId).maxBy {core.promptPriority(it)}
            warningStatus=if(config.mode=="Camera")when {
                !audio.ready->"Voice: $audioStatus"
                !cameraRunning->"Starting camera guidance"
                id!=0&&id==tactical->"Advice: ${core.reason()}"
                id!=0&&id==driving->"Advice: ${core.drivingReason()}"
                else->"Guidance: ${core.cameraReason()}"
            } else when {
                !audio.ready->"Voice: $audioStatus"
                !telemetry.calibrated->"Warnings: hold mount still for 5 seconds; rear camera forward"
                !telemetry.pitchValid->"Warnings: waiting for fresh motion sensor data"
                !telemetry.speedValid->"Warnings: GPS required; enable location or turn off Require GPS speed"
                else->when(core.reason()) {
                    "no actionable condition"->if(config.mode=="Desk")"Warnings: no simulated hazard; lower TTI to 2 s"
                        else if(result!=null&&!result.tti.isFinite())"Warnings: waiting for reliable approach / TTI evidence" else "Warnings: no approaching hazard"
                    "vision result expired"->"Warnings: vision result is too old; waiting for a current frame"
                    "cooldown"->"Warnings: 3.5-second voice cooldown"
                    "driver braking"->"Warnings muted: driver braking"
                    "IMU shock"->"Warnings muted: phone movement / shock"
                    else->"Warnings: ${core.reason()}"
                }
            }
            if(config.mode=="Camera") {
                warningStatus+="\nRoad: ${result?.takeIf {t-it.time>=0&&t-it.time<.35}?.road?.status?:"waiting for a fresh road view"} · Gear cues ${if(config.gear)"on" else "off"}"
                if(config.slope&&(!config.filmedVideo||config.phoneSlope)&&!telemetry.pitchValid)warningStatus+="\nSlope: normalize phone plane on level ground"
            }
            if(id!=0&&audio.submit(id)) {
                if(id in 2..4)core.accepted(t)
                core.drivingAccepted(id,t)
                if(config.mode=="Camera")core.cameraAccepted(id,t)
                main.post {prompt.text=core.promptText(id)}
            }
    }
    private val ruleTick=object:Runnable {
        override fun run() {if(resumed){evaluate();rules.postDelayed(this,20)}}
    }
    private fun slopeLabel(degrees:Double)=when(core.slopeBand(degrees)) {
        1->"Gentle uphill";2->"Uphill";3->"Steep uphill";4->"Very steep uphill"
        -1->"Gentle downhill";-2->"Downhill";-3->"Steep downhill";-4->"Very steep downhill"
        else->"Near level"
    }
    private val tick=object:Runnable {
        override fun run() {
            if(!resumed)return
            publishOptions()
            val measured=core.snapshot(now());val display=currentAdvice;val telemetry=display?.telemetry?:measured;val result=lastVision
            val htti=if(mode=="Desk")script?.at(now()-scriptStart)?.tti?:tti else result?.tti?:Double.POSITIVE_INFINITY
            val visionNow=now();val issue=if(mode=="Camera")cameraError?:visionError else null
            val live=result?.takeIf {CameraVisionHealth.canDisplay(visionNow,it.time,it.completedAt,if(mode=="Camera")cameraThermal else 0,issue)}
            overlay.result=if(mode=="Camera")live else null
            if(mode=="Camera") {
                visionStatus=CameraVisionHealth.status(visionNow,result?.time?:-1.0,result?.completedAt?:-1.0,lastCameraFrameAt,
                    cameraStartedAt,result?.detections?.size?:0,cameraThermal,issue,analysisBusy)
            } else if(live!=null) {
                visionStatus="Vision active: ${live.detections.size} road users detected"
            }
            if(mode=="Camera"&&live!=null&&prompt.text=="Camera · waiting for vision")prompt.text="Camera · live detections"
            val speedText=if(telemetry.speedValid)"Advice %.1f km/h (%s)".format(telemetry.speed,display?.speedSource?:"starting") else "Advice speed unavailable (${display?.speedSource?:"starting"})"
            val approach=if(htti.isFinite())"%.1f s".format(htti) else "not estimated"
            metrics.text="$speedText   |   pitch %.1f°   |   TTI $approach\nCalibration %d%% · %s · %s\nVision %.1f ms · %s".format(telemetry.pitch,(measured.progress*100).toInt(),if(telemetry.braking)"braking" else "coasting",if(measured.shock)"tap/shock" else "quiet",result?.processingMs?:0.0,audioStatus)
            if(mode=="Camera") {
                val phone=core.inclination(now())
                val usingPhone=!options.filmedVideo||options.phoneSlope
                val slopeText=if(usingPhone) {
                    if(phone.valid)"${slopeLabel(phone.degrees)} · estimated %+.1f° (%+.1f%%)".format(phone.degrees,phone.gradePercent)
                    else if(measured.calibrated)"Slope reading unavailable"
                    else "Phone slope unavailable; ${core.calibrationReason()}"
                } else "${slopeLabel(telemetry.pitch)} · simulated %+.1f° (%+.1f%%)".format(telemetry.pitch,100*kotlin.math.tan(telemetry.pitch*Math.PI/180))
                planeStatus.text=when {
                    !sensors.motionAvailable->sensors.motionStatus
                    !measured.calibrated->"Normalization ${(measured.progress*100).toInt()}% · ${core.calibrationReason()}"
                    !phone.valid->"Phone plane normalized · slope reading unavailable"
                    else->"Phone plane normalized · measured %+.1f° (%+.1f%%)".format(phone.degrees,phone.gradePercent)
                }
                val visionText="%.1f ms".format(result?.processingMs?:0.0)
                metrics.text="$speedText\n$slopeText\nTTI $approach · Vision $visionText · $audioStatus"
            }
            status.text="$visionStatus\n$warningStatus"
            main.postDelayed(this,50)
        }
    }
    override fun onResume() {super.onResume();resumed=true;if(::sensors.isInitialized)sensors.start();if(mode=="Camera"){inference.execute {visionFailure=null};startCamera()};main.post(tick);rules.post(ruleTick)}
    override fun onPause() {resumed=false;cameraRunning=false;generation.incrementAndGet();player?.close();camera?.unbindAll();sensors.close();audio.cancel();main.removeCallbacks(tick);rules.removeCallbacksAndMessages(null);super.onPause()}
    override fun onDestroy() {audio.close();sensors.close();player?.close();ruleThread.quitSafely();inference.execute {vision?.close();core.close()};inference.shutdown();main.removeCallbacksAndMessages(null);super.onDestroy()}
}
