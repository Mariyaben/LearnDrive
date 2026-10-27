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
import androidx.camera.camera2.interop.Camera2CameraInfo
import android.hardware.camera2.CameraCharacteristics
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger
import ai.learndrive.mobile.navigation.google.GoogleNavigationController
import ai.learndrive.mobile.navigation.google.GoogleNavigationUpdate
import ai.learndrive.mobile.navigation.google.GoogleNavigationPhase
import ai.learndrive.mobile.navigation.google.GoogleManeuverKind
import ai.learndrive.mobile.navigation.google.GoogleJourneyMemory
import androidx.lifecycle.ViewModelProvider
import ai.learndrive.mobile.ui.CoachStyle

class MainActivity : ComponentActivity() {
    private lateinit var core:NativeCore
    private lateinit var sensors:Sensors
    private lateinit var audio:AudioWorker
    private lateinit var coachingPreferences:CoachingPreferences
    @Volatile private var coachingSettings=CoachingSettings()
    private lateinit var preview:PreviewView
    private lateinit var overlay:DetectionOverlay
    private lateinit var cameraFrame:FrameLayout
    private lateinit var dashboard:ai.learndrive.mobile.ui.UnifiedDriveDashboard
    private lateinit var routeMap:ai.learndrive.mobile.navigation.OfflineNavigationMapSurface
    private var showSetup=true
    private var showDestination=false
    private var destinationRouting=false
    private var destinationSelectionPending=false
    private lateinit var navigationProviders:LinearLayout
    private var mapInteractive:Boolean?=null
    private var availableMapName:String?=null
    private var routeLoadNotice:String?=null
    private lateinit var routeStatus:TextView
    private lateinit var destinationPanel:ai.learndrive.mobile.navigation.DestinationPanel
    private var googleNavigation:GoogleNavigationController?=null
    private var useGoogle=false
    private lateinit var googleProviderButton:Button
    private lateinit var offlineProviderButton:Button
    private lateinit var destinationContent:FrameLayout
    private var googleEventKey:String?=null
    private val navigationCaptionGeneration=AtomicInteger()
    private var providerEvidenceAvailable=false
    private val activeMap:ai.learndrive.mobile.navigation.NavigationMapSurface
        get()=if(useGoogle)googleNavigation?:routeMap else routeMap
    private val routeMutationLock=Any()
    private var routeFileVersion=-1L
    @Volatile private var routeLoadGeneration=0
    private var lastNavigationTrace=0.0
    private var loadedRoute=false
    private var routeInstructions:Map<Int,String> = emptyMap()
    private val importRoutePack=registerForActivityResult(ActivityResultContracts.OpenDocument()){uri->
        if(uri!=null&&::destinationPanel.isInitialized)destinationPanel.importPack(uri)
    }
    private lateinit var status:TextView
    private lateinit var metrics:TextView
    private lateinit var prompt:TextView
    private val coachCaption=CoachCaption()
    private val practiceReview=PracticeSessionReview()
    private var lastPracticeReview:PracticeSessionReview.Review?=null
    private lateinit var reviewSession:Button
    private var sessionReviewDialog:android.app.AlertDialog?=null
    private lateinit var physical:Switch
    private lateinit var braking:Switch
    private lateinit var gear:Switch
    private lateinit var slope:Switch
    private lateinit var engineLoad:Switch
    private lateinit var filmedVideo:Switch
    private lateinit var phoneSlope:Switch
    private lateinit var researchEmergencies:Switch
    private lateinit var planeStatus:TextView
    private lateinit var cameraAdviceControls:LinearLayout
    private lateinit var deskControls:LinearLayout
    private val setup=DriveSetup()
    private val levelStart=LevelReferenceStart()
    private var sessionStartDialog:android.app.AlertDialog?=null
    private lateinit var setupPanel:LinearLayout
    private lateinit var setupStatus:TextView
    private lateinit var setupStart:Button
    private lateinit var referenceSkip:Button
    private lateinit var sessionEnd:Button
    private lateinit var labToggle:Button
    private lateinit var labPanel:LinearLayout
    private lateinit var rightPane:ScrollView
    private var showLab=false
    private val evidenceIo=Executors.newSingleThreadExecutor {r->Thread(r,"evidence-io")}
    private val evidenceLock=Any()
    private val evidenceRequest=AtomicInteger()
    private var evidenceStartPending=false
    @Volatile private var journal:SessionJournal?=null
    private lateinit var evidenceStatus:TextView
    private lateinit var evidenceStart:Button
    private lateinit var evidenceStop:Button
    private lateinit var evidenceExport:Button
    private lateinit var evidenceDelete:Button
    private lateinit var evidenceFile:java.io.File
    private var evidenceMetadata:String?=null
    private var evidenceBusy=true
    private var evidenceSaved=false
    private var evidenceNotice="Preparing local diagnostics"
    private var evidenceFinishedObserved=false
    private var frameJournalId="" // Inference executor only.
    private var frameJournalSource=-1L
    private var frameJournalTime:Double?=null
    private val main=Handler(Looper.getMainLooper())
    private val inference=Executors.newSingleThreadExecutor {r->Thread(r,"vision")}
    private val generation=AtomicInteger()
    private val ruleThread=HandlerThread("spatial-rules").also {it.start()}
    private val rules=Handler(ruleThread.looper)
    private data class Options(val mode:String="Camera",val speed:Double=60.0,val pitch:Double=0.0,val tti:Double=10.0,
        val physical:Boolean=false,val braking:Boolean=false,val gear:Boolean=true,val script:DeskScript?=null,val scriptStart:Double=0.0,
        val manualSpeed:Double=30.0,val slope:Boolean=true,val demoGear:Int=3,val demoRpm:Double=2000.0,val engineLoad:Boolean=false,
        val filmedVideo:Boolean=false,val videoPitch:Double=0.0,val phoneSlope:Boolean=true,val emergencies:Boolean=false,
        val sourceId:Long=0)
    @Volatile private var options=Options()
    private data class AdviceDisplay(val telemetry:Telemetry,val speedSource:String)
    @Volatile private var currentAdvice:AdviceDisplay?=null
    private var suppressed=false // Only accessed on the serial rules thread.
    private var camera:ProcessCameraProvider?=null
    private var cameraPreviewUseCase:Preview?=null
    private var cameraAnalysisUseCase:ImageAnalysis?=null
    private val displayListener=object:android.hardware.display.DisplayManager.DisplayListener {
        override fun onDisplayAdded(id:Int)=Unit
        override fun onDisplayRemoved(id:Int)=Unit
        override fun onDisplayChanged(id:Int) {
            if(!resumed||!::preview.isInitialized||preview.display?.displayId!=id)return
            val rotation=preview.display.rotation
            // A 180-degree landscape flip need not recreate the Activity. Both consumers
            // must use the current display orientation so inference isn't left upside down.
            cameraPreviewUseCase?.targetRotation=rotation
            cameraAnalysisUseCase?.targetRotation=rotation
        }
    }
    private var player:Mp4Source?=null
    @Volatile private var vision:Vision?=null // Mutated on inference executor; diagnostics read its immutable snapshot.
    private var visionFailure:Exception?=null
    @Volatile private var visionError:String?=null
    private var cameraError:String?=null
    @Volatile private var cameraThermal=0
    @Volatile private var lastCameraFrameAt=0.0
    @Volatile private var analysisBusy=false
    private var cameraStartedAt=0.0
    @Volatile private var lastVision:VisionResult?=null
    // Semantic-first publications are for rules. Keep completed camera evidence
    // stable for presentation and setup while the next spatial stage is running.
    @Volatile private var lastCompletedVision:VisionResult?=null
    // The semantic callback precedes spatial completion and has no FrameContext.
    // Stamp it at publication so the road HUD can use fresh boxes without
    // weakening the stable completed-result admission gate used by setup.
    private data class PublishedSemantic(val sourceId:Long,val result:VisionResult)
    @Volatile private var lastSemanticVision:PublishedSemantic?=null
    private var overlaySemantic:VisionResult?=null // Main-thread presentation cache only.
    private var overlaySpatial:VisionResult?=null
    private var roadOverlay:VisionResult?=null
    @Volatile private var resumed=false
    @Volatile private var cameraRunning=false
    private var cameraAwaitingLayout=false
    private var cameraPreviewWaitGeneration=0
    private var cameraSurfaceRetries=0
    @Volatile private var videoRunning=false
    private var traceKey=""
    private var traceAt=0.0
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
    private val exportEvidence=registerForActivityResult(ActivityResultContracts.CreateDocument("application/x-ndjson")) {uri->
        if(uri!=null) {
            evidenceBusy=true
            evidenceIo.execute {
                val message=runCatching {
                    check(journal?.isFinished!=false){"Stop capture before export"}
                    requireNotNull(contentResolver.openOutputStream(uri,"w")).use {out->evidenceFile.inputStream().use {it.copyTo(out)}}
                    "Diagnostics exported. Submission records do not prove audible playback."
                }.getOrElse {"Export failed: ${it.message}"}
                main.post {evidenceBusy=false;evidenceNotice=message}
            }
        }
    }
    private val pickScript=registerForActivityResult(ActivityResultContracts.OpenDocument()) {uri->
        if(uri!=null)try {
            val text=contentResolver.openInputStream(uri)?.bufferedReader()?.use {it.readText()}?:error("Cannot open CSV")
            script=DeskScript(text);scriptStart=now();switchMode("Desk");status.text="CSV trajectory running"
        }catch(e:Exception){status.text="CSV: ${e.message}"}
    }
    private val permissions=registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        sensors.start();if(mode=="Camera"&&!cameraRunning)startCamera()
    }
    private fun text(value:String,size:Float=16f)=CoachStyle.label(this,value,size).apply {setPadding(0,CoachStyle.dp(this@MainActivity,5),0,CoachStyle.dp(this@MainActivity,5))}
    private fun button(value:String,action:()->Unit)=CoachStyle.button(this,value).apply {setOnClickListener {action()} }
    private fun TextView.setUiText(value:String) {if(text.toString()!=value)text=value}
    private fun hideKeyboard(){currentFocus?.clearFocus();getSystemService(android.view.inputmethod.InputMethodManager::class.java)?.hideSoftInputFromWindow(window.decorView.windowToken,0)}
    private fun openDestination() {
        if(setup.isMonitoring()||knownMoving())return
        destinationSelectionPending=false;destinationRouting=false
        showSetup=true;showDestination=true;updateSetupUi()
        if(useGoogle)googleNavigation?.requestDestinationFocus() else {
            destinationPanel.search.requestFocus()
            getSystemService(android.view.inputmethod.InputMethodManager::class.java)?.showSoftInput(destinationPanel.search,android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT)
        }
    }
    override fun onCreate(saved:Bundle?) {
        super.onCreate(saved)
        androidx.core.view.WindowCompat.setDecorFitsSystemWindows(window,false)
        getSystemService(android.hardware.display.DisplayManager::class.java).registerDisplayListener(displayListener,main)
        volumeControlStream=AudioManager.STREAM_MUSIC;core=NativeCore();sensors=Sensors(this,core)
        coachingPreferences=CoachingPreferences(getSharedPreferences("driving_advice",MODE_PRIVATE))
        coachingSettings=coachingPreferences.load();core.configureCoaching(coachingSettings)
        preview=PreviewView(this).apply {scaleType=PreviewView.ScaleType.FIT_CENTER;implementationMode=PreviewView.ImplementationMode.COMPATIBLE}
        overlay=DetectionOverlay(this)
        cameraFrame=FrameLayout(this).apply {addView(preview,FrameLayout.LayoutParams(-1,-1));addView(this@MainActivity.overlay,FrameLayout.LayoutParams(-1,-1))}
        dashboard=ai.learndrive.mobile.ui.UnifiedDriveDashboard(this,cameraFrame)
        val outer=dashboard
        routeMap=ai.learndrive.mobile.navigation.OfflineNavigationMapSurface(this,saved)
        dashboard.attachMap(routeMap.view)
        prompt=dashboard.coachText;metrics=dashboard.detailsText;status=dashboard.roadStatus;routeStatus=dashboard.maneuverText
        sessionEnd=button("End session") {endRoadSession()}.apply {visibility=View.GONE;textSize=14f;isAllCaps=false}
        dashboard.sessionActionHost.addView(sessionEnd,FrameLayout.LayoutParams(-2,-1))
        dashboard.destinationButton.setOnClickListener {openDestination()}
        dashboard.destinationTab.setOnClickListener {openDestination()}
        dashboard.prepareTab.setOnClickListener {hideKeyboard();showDestination=false;updateSetupUi()}
        dashboard.closeSetupButton.setOnClickListener {hideKeyboard();showSetup=false;updateSetupUi()}
        dashboard.quickStartButton.setOnClickListener {if(!setup.isMonitoring()&&!knownMoving()){showDestination=false;showSetup=true;updateSetupUi()}}
        dashboard.settingsButton.setOnClickListener {hideKeyboard();showDestination=false;showSetup=!showSetup;updateSetupUi()}
        rightPane=ScrollView(this).apply {isVerticalScrollBarEnabled=false;isFillViewport=true}
        dashboard.setupContentHost.addView(rightPane,FrameLayout.LayoutParams(-1,-1))
        val sidebar=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL};rightPane.addView(sidebar)
        setupPanel=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL};sidebar.addView(setupPanel)
        destinationPanel=ai.learndrive.mobile.navigation.DestinationPanel(this,{sensors.latestPosition},
            {requestPermissions(includeLocation=true)},
            {routeFileVersion=-1L;loadOfflineRoute();showDestination=false;updateSetupUi()}, {clearOfflineRoute()},
            {pack->if(!loadedRoute){availableMapName=pack.name;routeMap.showPack(pack)}})
        val destinationControls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL}
        val destinationScroll=ScrollView(this).apply {isFillViewport=true;isVerticalScrollBarEnabled=false;addView(destinationControls)}
        dashboard.destinationHost.addView(destinationScroll,FrameLayout.LayoutParams(-1,-1))
        val providers=LinearLayout(this)
        navigationProviders=providers
        googleProviderButton=button("Google Maps") {selectNavigationProvider(true,true)}.apply {tag="google-provider"}
        offlineProviderButton=button("Offline region") {selectNavigationProvider(false,true)}.apply {tag="offline-provider"}
        providers.addView(googleProviderButton,LinearLayout.LayoutParams(0,-2,1f).apply {marginEnd=CoachStyle.dp(this@MainActivity,6)})
        providers.addView(offlineProviderButton,LinearLayout.LayoutParams(0,-2,1f))
        destinationControls.addView(providers)
        destinationContent=FrameLayout(this);destinationControls.addView(destinationContent)
        if(BuildConfig.GOOGLE_NAVIGATION_AVAILABLE) {
            val key=packageManager.getApplicationInfo(packageName,PackageManager.GET_META_DATA).metaData
                ?.getString("com.google.android.geo.API_KEY").orEmpty()
            googleNavigation=GoogleNavigationController(this,key,
                onState={if(useGoogle&&!isDestroyed){
                    dashboard.setDestination(it.destinationLabel.takeIf(String::isNotBlank))
                    if(it.routing&&destinationSelectionPending)destinationRouting=true
                    else if(!it.routing&&destinationRouting){destinationRouting=false;destinationSelectionPending=false;if(it.routeReady&&showDestination&&::setupStart.isInitialized){showDestination=false;updateSetupUi()}}
                }},
                onUpdate={if(useGoogle&&!isDestroyed)acceptGoogleNavigation(it)},
                journey=ViewModelProvider(this)[GoogleJourneyMemory::class.java],
                onDestinationSelected={destinationSelectionPending=true})
        }
        useGoogle=getSharedPreferences("navigation_provider",MODE_PRIVATE).getBoolean("google",true)&&googleNavigation!=null
        showNavigationProvider()
        setupPanel.addView(text("Parked, mounted, supervised.",12f).apply {setTextColor(CoachStyle.muted)})
        setupPanel.addView(dashboard.readiness)
        val setupUtilities=LinearLayout(this)
        setupPanel.addView(setupUtilities)
        setupUtilities.addView(button("Check sound") {
            audio.cancel();if(!audio.submit(2))status.text="Voice unavailable: $audioStatus · check media volume"
        },LinearLayout.LayoutParams(0,CoachStyle.dp(this,48),1f).apply {marginEnd=CoachStyle.dp(this@MainActivity,6)})
        val moreOptions=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL;visibility=View.GONE}
        val optionsToggle=button("More setup options") {}
        optionsToggle.setOnClickListener {val opening=moreOptions.visibility==View.GONE;moreOptions.visibility=if(opening)View.VISIBLE else View.GONE;optionsToggle.text=if(opening)"Hide setup options" else "More setup options"}
        setupUtilities.addView(optionsToggle,LinearLayout.LayoutParams(0,CoachStyle.dp(this,48),1f));setupPanel.addView(moreOptions)
        moreOptions.addView(text("Coaching level",13f))
        val coachingProfile=Spinner(this).apply {
            tag="coaching-profile"
            adapter=ArrayAdapter(this@MainActivity,android.R.layout.simple_spinner_dropdown_item,CoachingProfile.entries.map {it.label})
            setSelection(coachingSettings.profile.ordinal)
        }
        coachingProfile.onItemSelectedListener=object:AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent:AdapterView<*>?,view:View?,position:Int,id:Long) {
                changeCoachingSettings(CoachingProfile.entries[position],coachingSettings.voiceEnabled)
            }
            override fun onNothingSelected(parent:AdapterView<*>?)=Unit
        }
        moreOptions.addView(coachingProfile)
        moreOptions.addView(Switch(this).apply {
            tag="coaching-voice";text="Coaching voice";setTextColor(CoachStyle.text);isChecked=coachingSettings.voiceEnabled
            setOnCheckedChangeListener {_,enabled->changeCoachingSettings(coachingSettings.profile,enabled)}
        })
        moreOptions.addView(text("Minimal keeps essential cues. Balanced spaces routine coaching. Guided adds more reminders. Visual coaching continues with voice off.",12f))
        moreOptions.addView(button("Enable GPS speed") {requestPermissions(includeLocation=true)})
        moreOptions.addView(button("Recalibrate level reference") {normalizePhone()})
        referenceSkip=button("Skip inclination lessons") {setup.skipLevelReference();publishOptions();updateSetupUi()}
        moreOptions.addView(referenceSkip)
        moreOptions.addView(text("Inclination lessons need a level reference. Confirm level ground only when parked.",12f))
        setupStatus=text("Checking camera, motion and voice",12f).apply {setTextColor(CoachStyle.muted)};moreOptions.addView(setupStatus)
        setupPanel.addView(text("Supervised evaluation · keep watching the road",11f).apply {setTextColor(CoachStyle.muted)})
        setupStart=button("Start session") {confirmRoadSession()};CoachStyle.styleButton(setupStart,true)
        dashboard.setupActionHost.orientation=LinearLayout.HORIZONTAL
        reviewSession=button("Last session") {openSessionReview()}.apply {visibility=View.GONE}
        dashboard.setupActionHost.addView(reviewSession,LinearLayout.LayoutParams(0,CoachStyle.dp(this,52),1f).apply {marginEnd=CoachStyle.dp(this@MainActivity,6)})
        dashboard.setupActionHost.addView(setupStart,LinearLayout.LayoutParams(0,CoachStyle.dp(this,52),1.5f))
        labToggle=button("Lab settings and test sources") {showLab=!showLab;updateSetupUi()};sidebar.addView(labToggle)
        val controls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL;visibility=View.GONE};labPanel=controls;sidebar.addView(controls)
        metrics.visibility=View.VISIBLE;controls.addView(metrics)
        controls.addView(button("Add offline region file") {
            if(!setup.isMonitoring()&&!knownMoving())importRoutePack.launch(arrayOf("application/zip","application/octet-stream","*/*"))
        })
        evidenceFile=java.io.File(filesDir,"diagnostics/session.jsonl")
        evidenceStatus=text(evidenceNotice,13f);controls.addView(evidenceStatus)
        evidenceStart=button("Start local diagnostics") {startEvidence()};controls.addView(evidenceStart)
        evidenceStop=button("Stop diagnostics") {journal?.stop("user_stop")};controls.addView(evidenceStop)
        evidenceExport=button("Export diagnostics (JSONL)") {if(!evidenceBusy&&evidenceSaved&&journal?.isFinished!=false)exportEvidence.launch("LearnDrive-session.jsonl")};controls.addView(evidenceExport)
        evidenceDelete=button("Delete local diagnostics") {deleteEvidence()};controls.addView(evidenceDelete)
        evidenceIo.execute {
            val metadata=runCatching {
                val pkg=packageManager.getPackageInfo(packageName,0)
                org.json.JSONObject().apply {
                    put("app_version",pkg.versionName);put("version_code",pkg.longVersionCode)
                    put("device",android.os.Build.MODEL);put("manufacturer",android.os.Build.MANUFACTURER)
                    put("android_release",android.os.Build.VERSION.RELEASE);put("api_level",android.os.Build.VERSION.SDK_INT)
                    put("os_build",android.os.Build.DISPLAY)
                    put("models",org.json.JSONObject(assets.open("model-manifest.json").bufferedReader().use {it.readText()}))
                    put("spatial_model",org.json.JSONObject(assets.open("supercombo-manifest.json").bufferedReader().use {it.readText()}))
                    put("decision_sampling","changes_or_at_least_once_per_second");put("frame_sampling","semantic_updates_up_to_8_hz")
                    put("camera_coaching_policy","temporal_attention_v1");put("semantic_publication","before_spatial_inference")
                    put("navigation_policy","provider_turn_preparation_v1");put("navigation_runtime","Google Navigation 7.6.1 / local Valhalla Mobile 0.6.4")
                    put("navigation_evidence","Provider estimates and measured GPS quality; no route/GPS coordinates exported")
                    put("coaching_policy","balanced_coaching_v1");put("capability_policy","observed_spatial_budget_v1")
                    put("audio_evidence","worker submission acceptance; completion and audible onset not measured")
                    put("contains_video",false);put("contains_microphone_audio",false);put("contains_gps_coordinates",false)
                    put("recording_sidecar_import_supported",false)
                }.toString()
            }
            val exists=evidenceFile.isFile
            main.post {evidenceMetadata=metadata.getOrNull();evidenceSaved=exists;evidenceBusy=false
                evidenceNotice=if(exists)"Saved diagnostics available; export or delete before a new capture" else
                    if(metadata.isSuccess)"Diagnostics off · one local file, up to 8 MiB · no video/audio/coordinates" else "Diagnostic metadata unavailable"}
        }
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
        filmedVideo=Switch(this).apply {text="Camera pointed at a road video (test)";isChecked=false}
        cameraAdviceControls.addView(filmedVideo)
        phoneSlope=Switch(this).apply {text="Use normalized phone slope";isChecked=preferences.getBoolean("phoneSlope",true)}
        cameraAdviceControls.addView(phoneSlope)
        planeStatus=text("Normalize the mounted phone on level ground",14f);cameraAdviceControls.addView(planeStatus)
        cameraAdviceControls.addView(button("Normalize phone plane · level ground · hold still 5 s") {
            normalizePhone()
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
            if(::audio.isInitialized){audio.cancel();publishOptions();rules.post {core.drivingReset();core.dualPolicyReset()}}
        }
        val gpsButton=button("Enable GPS speed (optional)") {requestPermissions(includeLocation=true)};cameraAdviceControls.addView(gpsButton)
        val fallbackControls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL}
        cameraAdviceControls.addView(fallbackControls)
        slider(fallbackControls,"Video driving speed (test)",160,manualSpeed.toInt(),"km/h") {
            manualSpeed=it;preferences.edit().putInt("manualSpeed",it.toInt()).apply()
        }
        fallbackControls.addView(text("Used only for screen-video tests. Live road speed uses measurements; missing speed stays unavailable.",13f))
        fallbackControls.visibility=if(filmedVideo.isChecked)View.VISIBLE else View.GONE
        gpsButton.isEnabled=!filmedVideo.isChecked
        filmedVideo.setOnCheckedChangeListener {_,testing ->
            setup.stop();practiceReview.discard()
            videoControls.visibility=if(testing&&!phoneSlope.isChecked)View.VISIBLE else View.GONE
            phoneSlope.visibility=if(testing)View.VISIBLE else View.GONE
            gpsButton.isEnabled=!testing
            fallbackControls.visibility=if(testing)View.VISIBLE else View.GONE
            if(::audio.isInitialized){audio.cancel();if(resumed&&mode=="Camera")startCamera();publishOptions()}
        }
        val recordingControls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL}
        controls.addView(recordingControls)
        recordingControls.addView(text("MP4 tests: set the recorded vehicle speed manually. Original camera calibration and motion telemetry are not loaded yet.",13f))
        slider(recordingControls,"Recorded speed (manual test)",160,manualSpeed.toInt(),"km/h") {manualSpeed=it}
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
        deskControls.addView(button("Normalize phone plane · level ground · hold still 5 s") {normalizePhone();status.text="Rear camera forward; keep mount stationary on level ground"})
        controls.addView(button("Test audio preemption") {audio.cancel();audio.submit(1);main.postDelayed({audio.submit(4)},250)})
        controls.addView(text("Camera speaks when it starts, then gives guidance for visible road users and large obstacles in the path ahead. This guidance works without calibration, GPS or TTI. Motion evidence controls imminent-collision warnings. Use Test voice to check sound.",13f))
        deskControls.visibility=View.GONE
        setContentView(outer)
        var previousKeyboard=false
        androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(outer) {v,insets->
            val bars=insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.systemBars() or androidx.core.view.WindowInsetsCompat.Type.displayCutout())
            val ime=insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.ime())
            val typing=insets.isVisible(androidx.core.view.WindowInsetsCompat.Type.ime())
            val side=CoachStyle.dp(this,12);val vertical=CoachStyle.dp(this,8)
            v.setPadding(side+bars.left,vertical+bars.top,side+bars.right,vertical+maxOf(bars.bottom,ime.bottom))
            dashboard.setKeyboardVisible(typing)
            googleNavigation?.setKeyboardVisible(typing)
            navigationProviders.visibility=if(typing&&showDestination)View.GONE else View.VISIBLE
            if(previousKeyboard!=typing){previousKeyboard=typing;destinationScroll.post {destinationScroll.scrollTo(0,0)}}
            insets
        }
        onBackPressedDispatcher.addCallback(this,object:androidx.activity.OnBackPressedCallback(true){override fun handleOnBackPressed(){
            when {showDestination->{hideKeyboard();showDestination=false;updateSetupUi()}
                showLab->{showLab=false;updateSetupUi()}
                showSetup->{hideKeyboard();showSetup=false;updateSetupUi()}
                else->finish()}
        }})
        audio=AudioWorker(this,core::promptMayInterrupt,core.promptCount(),core::promptPriority) {message->main.post {audioStatus=message}}
        audio.setVoiceEnabled(coachingSettings.voiceEnabled)
        core.bindAudioDispatcher(ai.learndrive.mobile.audio.AudioDispatcher(audio))
        researchEmergencies=Switch(this).apply {text="Lab only: unvalidated emergency commands";isChecked=false}
        controls.addView(researchEmergencies)
        researchEmergencies.setOnCheckedChangeListener {_,_->audio.cancel();publishOptions()}
        requestPermissions()
        if(useGoogle)googleNavigation?.resumeIfPreviouslyAccepted()
        evidenceIo.execute {
            val pack=runCatching {ai.learndrive.mobile.navigation.OfflinePack.installed(this)
                .sortedWith(compareByDescending<ai.learndrive.mobile.navigation.OfflinePack>{it.metadata.optInt("places_schema",1)}.thenByDescending {it.id}).firstOrNull()}.getOrNull()
            main.post {if(!isDestroyed&&!loadedRoute){availableMapName=pack?.name;routeMap.showPack(pack)}}
        }
    }
    private fun roadSource()=mode=="Camera"&&!filmedVideo.isChecked
    private fun changeCoachingSettings(profile:CoachingProfile,voice:Boolean) {
        if(coachingSettings.profile==profile&&coachingSettings.voiceEnabled==voice)return
        synchronized(core) {
            val updated=CoachingSettings(profile,voice,Math.addExact(coachingSettings.revision,1))
            core.configureCoaching(updated);coachingSettings=updated
            // Same lock order as native delivery/onset: core, then audio.
            if(::audio.isInitialized){audio.cancel();audio.setVoiceEnabled(voice)}
            coachingPreferences.save(updated)
        }
        coachCaption.clear()
        if(::researchEmergencies.isInitialized)publishOptions()
    }
    private fun showNavigationProvider() {
        dashboard.attachMap(activeMap.view)
        destinationContent.removeAllViews()
        destinationContent.addView(if(useGoogle)requireNotNull(googleNavigation).destinationView else destinationPanel.view,
            FrameLayout.LayoutParams(-1,-2))
        googleProviderButton.isSelected=useGoogle
        offlineProviderButton.isSelected=!useGoogle
        googleProviderButton.alpha=if(useGoogle)1f else .65f
        offlineProviderButton.alpha=if(useGoogle).65f else 1f
        dashboard.maneuverProvider.text=if(useGoogle)"Google Maps" else "Offline map"
        mapInteractive=null
        dashboard.setDestination(if(useGoogle)googleNavigation?.state?.destinationLabel?.takeIf {it.isNotBlank()} else null)
    }
    /** Changes route context only; the camera, source identity and models stay live. */
    private fun selectNavigationProvider(google:Boolean,fromUser:Boolean) {
        if(setup.isMonitoring()||knownMoving()||(google&&googleNavigation==null))return
        if(useGoogle!=google) {
            googleNavigation?.stopGuidance();audio.cancel();coachCaption.clear();googleEventKey=null;navigationCaptionGeneration.incrementAndGet()
            synchronized(routeMutationLock){++routeLoadGeneration;loadedRoute=false;core.navigationClear();useGoogle=google}
            routeFileVersion=-1L
            getSharedPreferences("navigation_provider",MODE_PRIVATE).edit().putBoolean("google",google).apply()
            showNavigationProvider()
        }
        if(google) {
            requestPermissions(includeLocation=true)
            if(fromUser)googleNavigation?.enableFromUserAction() else googleNavigation?.resumeIfPreviouslyAccepted()
        } else loadOfflineRoute()
        updateSetupUi()
    }
    private fun acceptGoogleNavigation(update:GoogleNavigationUpdate?) {
        val key=update?.let {"${it.sessionId}:${it.routeEpoch}:${it.stepNumber}:${it.state}:${it.maneuver}:${it.roundaboutExit}"}
        fun retirePreviousCue() {
            if(key!=googleEventKey) {
                navigationCaptionGeneration.incrementAndGet();coachCaption.clearNavigation();googleEventKey=key
                if(::audio.isInitialized)audio.cancelIfPrompt {it in 47..55}
            }
        }
        if(update==null||!resumed||!roadSource()) {
            synchronized(routeMutationLock){core.navigationTurnUnavailable()}
            retirePreviousCue();return
        }
        val phase=when(update.state) {
            GoogleNavigationPhase.ENROUTE->1;GoogleNavigationPhase.REROUTING->2
            GoogleNavigationPhase.STOPPED->3;GoogleNavigationPhase.ARRIVED->4;else->0
        }
        val maneuver=when(update.maneuver) {
            GoogleManeuverKind.LEFT_TURN,GoogleManeuverKind.SLIGHT_LEFT,GoogleManeuverKind.SHARP_LEFT->1
            GoogleManeuverKind.RIGHT_TURN,GoogleManeuverKind.SLIGHT_RIGHT,GoogleManeuverKind.SHARP_RIGHT->2
            GoogleManeuverKind.KEEP_LEFT,GoogleManeuverKind.KEEP_RIGHT,GoogleManeuverKind.FORK_LEFT,GoogleManeuverKind.FORK_RIGHT->3
            GoogleManeuverKind.ON_RAMP,GoogleManeuverKind.OFF_RAMP->4
            GoogleManeuverKind.MERGE->5;GoogleManeuverKind.U_TURN->6;GoogleManeuverKind.ROUNDABOUT->7
            GoogleManeuverKind.STRAIGHT->8;GoogleManeuverKind.ARRIVAL->9;else->0
        }
        synchronized(routeMutationLock) {
            core.navigationTurn(1,update.sessionId,update.routeEpoch,update.sequence,update.receivedAtSeconds,
                phase,update.stepNumber?:-1,maneuver,update.distanceMeters?:Double.POSITIVE_INFINITY,
                update.timeSeconds?:Double.POSITIVE_INFINITY)
        }
        // Invalidate native pending decisions before retiring already submitted
        // PCM and queued captions. Critical camera speech keeps its own lifetime.
        retirePreviousCue()
    }
    private fun googleManeuverText(update:GoogleNavigationUpdate):String=when(update.maneuver) {
        GoogleManeuverKind.LEFT_TURN->"Turn left";GoogleManeuverKind.RIGHT_TURN->"Turn right"
        GoogleManeuverKind.SLIGHT_LEFT->"Slight left";GoogleManeuverKind.SLIGHT_RIGHT->"Slight right"
        GoogleManeuverKind.SHARP_LEFT->"Sharp left";GoogleManeuverKind.SHARP_RIGHT->"Sharp right"
        GoogleManeuverKind.KEEP_LEFT,GoogleManeuverKind.FORK_LEFT->"Keep left at the fork"
        GoogleManeuverKind.KEEP_RIGHT,GoogleManeuverKind.FORK_RIGHT->"Keep right at the fork"
        GoogleManeuverKind.MERGE->"Merge ahead";GoogleManeuverKind.ON_RAMP->"Join the ramp"
        GoogleManeuverKind.OFF_RAMP->"Take the exit";GoogleManeuverKind.U_TURN->"U-turn ahead"
        GoogleManeuverKind.ROUNDABOUT->update.roundaboutExit?.let {"Roundabout · exit $it"}?:"Roundabout ahead"
        GoogleManeuverKind.ARRIVAL->"Destination ahead";GoogleManeuverKind.STRAIGHT->"Continue ahead"
        else->"Follow the mapped route"
    }
    private fun startEvidence() {
        if(evidenceBusy||evidenceSaved||journal?.isFinished==false||knownMoving()||evidenceMetadata==null)return
        android.app.AlertDialog.Builder(this).setTitle("Record local diagnostic evidence?")
            .setMessage("Stores device/model versions, frame geometry, sampled detections and motion estimates, decisions and accepted audio submissions. No images, video, microphone audio or GPS coordinates. One file up to 8 MiB; stops on backgrounding. Export or delete it before another capture.")
            .setNegativeButton("Back",null).setPositiveButton("Start diagnostics") {_,_->
                if(resumed&&!evidenceBusy&&!evidenceSaved&&!knownMoving()&&journal?.isFinished!=false) {
                    val metadata=org.json.JSONObject(evidenceMetadata!!).apply {put("started_utc",java.time.Instant.now().toString());put("started_monotonic",now())}.toString()
                    evidenceBusy=true;evidenceStartPending=true;evidenceFinishedObserved=false
                    val request=evidenceRequest.incrementAndGet()
                    rules.post {
                        var startError:String?=null
                        synchronized(evidenceLock) {
                            if(resumed&&request==evidenceRequest.get()) {
                                runCatching {
                                    journal=SessionJournal(evidenceFile,metadata);traceKey="";traceAt=0.0
                                    val config=options
                                    val source=when(config.mode){"Desk"->CaptureSource.SIMULATION;"MP4"->CaptureSource.RECORDING;else->if(config.filmedVideo)CaptureSource.SCREEN else CaptureSource.CAMERA}
                                    journal?.offer(GuidanceTrace.source(core.sourceId,source,now()))
                                }.onFailure {startError="Could not start diagnostics: ${it.message}"}
                            }
                        }
                        main.post {if(request==evidenceRequest.get()){evidenceStartPending=false;evidenceBusy=false;evidenceNotice=startError?:"Diagnostics enabled for this foreground interval"}}
                    }
                }
            }.show()
    }
    private fun deleteEvidence() {
        if(evidenceBusy||journal?.isFinished==false||knownMoving())return
        android.app.AlertDialog.Builder(this).setTitle("Delete local diagnostics?")
            .setMessage("Deletes the app's saved diagnostic file. Exported copies remain where you saved them.")
            .setNegativeButton("Keep",null).setPositiveButton("Delete") {_,_->
                if(evidenceBusy||journal?.isFinished==false)return@setPositiveButton
                evidenceBusy=true
                evidenceIo.execute {
                    val deleted=!evidenceFile.exists()||evidenceFile.delete()
                    main.post {evidenceBusy=false;if(deleted){journal=null;evidenceSaved=false;evidenceNotice="Local diagnostics deleted"}else evidenceNotice="Could not delete local diagnostics"}
                }
            }.show()
    }
    private fun recordFrame(result:VisionResult) {
        val target=journal?:return;if(!target.isAccepting)return
        if(frameJournalId!=target.sessionId){frameJournalId=target.sessionId;frameJournalSource=-1;frameJournalTime=null}
        if(frameJournalSource!=result.frame?.sourceId||frameJournalTime!=result.semanticCaptureTime) {
            runCatching {target.offer(GuidanceTrace.frame(result))}.onFailure {target.recordLoss();Log.w("LearnDriveEvidence","Could not serialize frame evidence",it)}
            frameJournalSource=result.frame?.sourceId?:-1;frameJournalTime=result.semanticCaptureTime
        }
    }
    private fun updateEvidenceUi() {
        val target=journal
        if(target?.isFinished==true&&!evidenceFinishedObserved&&!evidenceBusy) {
            evidenceFinishedObserved=true;evidenceBusy=true
            evidenceIo.execute {val exists=evidenceFile.isFile;main.post {evidenceSaved=exists;evidenceBusy=false;evidenceNotice=if(exists)"Saved locally; export or delete before a new capture" else "No diagnostic file was saved"}}
        }
        evidenceStatus.text=if(target!=null)"${target.status()}\n$evidenceNotice" else evidenceNotice
        evidenceStart.isEnabled=!evidenceBusy&&!evidenceSaved&&target?.isFinished!=false&&evidenceMetadata!=null&&!knownMoving()
        evidenceStop.isEnabled=target?.isAccepting==true
        evidenceExport.isEnabled=!evidenceBusy&&evidenceSaved&&target?.isFinished!=false
        evidenceDelete.isEnabled=!evidenceBusy&&evidenceSaved&&target?.isFinished!=false&&!knownMoving()
    }
    private fun knownMoving():Boolean {
        val measured=core.snapshot(now())
        return measured.speedValid&&measured.speed.isFinite()&&measured.speed>1
    }
    private fun canStartRoad():Boolean=canStartRoad(knownMoving())
    private fun canStartRoad(moving:Boolean):Boolean {
        if(!roadSource()||setup.isMonitoring()||setup.referencePending()||!cameraRunning||moving)return false
        val frame=lastCompletedVision?:return false;val age=now()-frame.time
        return frame.frame?.sourceId==generation.get().toLong()&&age>=0&&age<.35&&visionError==null&&cameraError==null&&cameraThermal<4&&
            frame.spatialReadiness!=SpatialReadiness.MOUNT_UNSUPPORTED
    }
    private fun confirmRoadSession() {
        if(!roadSource()) {
            if(mode!="Camera")switchMode("Camera")
            filmedVideo.isChecked=false;requestPermissions();return
        }
        if(!resumed||knownMoving()||setup.isMonitoring()){updateSetupUi();return}
        if(sessionStartDialog?.isShowing==true)return
        val confirmation=android.app.AlertDialog.Builder(this).setTitle("Check the mount while parked")
            .setMessage("Keep the phone firmly mounted, rear camera forward and road view unobstructed. If parked on level ground, confirm below: the app calibrates the level reference automatically, then starts after fresh camera evidence. Hold still for about 5 seconds. The phone cannot verify level ground.\n\n${startReadinessMessage()}")
            .setNegativeButton("Back",null).setNeutralButton("Skip slope lessons",null)
            .setPositiveButton("Level ground · start",null).create()
        sessionStartDialog=confirmation
        confirmation.setOnDismissListener {
            if(sessionStartDialog===confirmation)sessionStartDialog=null
            if(levelStart.isPending){levelStart.cancel();setup.skipLevelReference();publishOptions()}
        }
        confirmation.setOnShowListener {
            confirmation.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                if(levelStart.isPending)return@setOnClickListener
                val current=lastCompletedVision?.takeIf {it.frame?.sourceId==generation.get().toLong()&&now()-it.time in 0.0..<.35}
                if(!resumed||!roadSource()||setup.isMonitoring()||knownMoving()||cameraThermal>=4||cameraError!=null||visionError!=null||current?.spatialReadiness==SpatialReadiness.MOUNT_UNSUPPORTED) {
                    setStartDialogMessage("Session has not started. ${startReadinessMessage()} Stay parked, then retry.")
                } else if(!sensors.motionAvailable) {
                    setStartDialogMessage("Level reference unavailable: ${sensors.motionStatus}. You can start without inclination lessons when the camera is ready.")
                } else {
                    val needsReference=!setup.levelAllowed(core.calibrationId())
                    if(needsReference)resetPhoneReference(true)
                    check(levelStart.request(now(),generation.get().toLong()))
                    confirmation.getButton(android.app.AlertDialog.BUTTON_POSITIVE).apply {text=if(needsReference)"Calibrating…" else "Starting…";isEnabled=false}
                    setStartDialogMessage(if(needsReference)"Hold still on the confirmed level ground. Calibrating the level reference; coaching starts only after calibration and fresh camera evidence. Back cancels this start."
                        else "Level reference is ready. Waiting for current camera evidence. Stay parked; Back cancels this start.")
                }
                updateSetupUi()
            }
            confirmation.getButton(android.app.AlertDialog.BUTTON_NEUTRAL).setOnClickListener {
                levelStart.cancel();setup.skipLevelReference();publishOptions()
                if(!resumed||!roadSource()||setup.isMonitoring()||knownMoving()) {
                    setStartDialogMessage("Session has not started. ${startReadinessMessage()}")
                } else {
                    check(levelStart.request(now(),generation.get().toLong(),false))
                    confirmation.getButton(android.app.AlertDialog.BUTTON_POSITIVE).apply {text="Starting…";isEnabled=false}
                    setStartDialogMessage("Starting without inclination lessons. Waiting for current camera evidence. Stay parked; Back cancels this start.")
                }
            }
        }
        confirmation.show()
    }
    private fun startReadinessMessage():String=when {
        knownMoving()->"Park safely before starting."
        cameraThermal>=4->"The phone is too warm for camera coaching. Let it cool."
        cameraError!=null->"Camera unavailable: $cameraError"
        visionError!=null->"Camera analysis unavailable: $visionError"
        !cameraRunning->"Waiting for the rear camera."
        setup.referencePending()->"Hold still while the level reference is calibrated."
        lastCompletedVision?.frame?.sourceId!=generation.get().toLong()->"Waiting for a current camera result."
        lastCompletedVision?.spatialReadiness==SpatialReadiness.MOUNT_UNSUPPORTED->"Adjust the mount so the rear camera faces forward towards the road."
        lastCompletedVision?.let {now()-it.time !in 0.0..<.35}!=false->"Waiting for fresh camera evidence."
        else->"Camera is ready. Voice is optional."
    }
    private fun setStartDialogMessage(message:String) {
        sessionStartDialog?.findViewById<TextView>(android.R.id.message)?.setUiText(message)
    }
    private fun commitRoadSession():Boolean {
        if(!canStartRoad())return false
        val t=now();val up=core.cameraUp(t)
        if(!setup.start(t,up[0],up[1],up[2]))return false
        levelStart.cancel();practiceReview.start(t);sessionStartDialog?.dismiss()
        audio.cancel();activeMap.setFollowing(true)
        if(useGoogle)googleNavigation?.startGuidance()
        startCamera();publishOptions();updateSetupUi();return true
    }
    private fun updateAutomaticLevelStart() {
        if(!levelStart.isPending)return
        val result=levelStart.poll(now(),generation.get().toLong(),resumed&&roadSource(),knownMoving(),
            (!levelStart.requiresReference()||sensors.motionAvailable)&&cameraError==null&&visionError==null&&cameraThermal<4,
            setup.referencePending(),setup.levelAllowed(core.calibrationId()),canStartRoad())
        when(result) {
            LevelReferenceStart.READY->if(!commitRoadSession()) {
                sessionStartDialog?.getButton(android.app.AlertDialog.BUTTON_POSITIVE)?.apply {text="Level ground · start";isEnabled=true}
                setStartDialogMessage("Session has not started. ${startReadinessMessage()} Retry while parked.")
            }
            LevelReferenceStart.CANCELLED->{
                setup.skipLevelReference();publishOptions()
                sessionStartDialog?.getButton(android.app.AlertDialog.BUTTON_POSITIVE)?.apply {text="Level ground · start";isEnabled=true}
                setStartDialogMessage("Automatic start cancelled: calibration did not finish in time, or the setup changed. ${startReadinessMessage()} Confirm level ground again when ready; coaching has not started.")
            }
        }
    }
    private fun endRoadSession() {
        val review=practiceReview.finish(now())
        if(review!=null)lastPracticeReview=review
        setup.stop();audio.cancel();coachCaption.clear();googleNavigation?.stopGuidance();showSetup=true
        if(resumed&&mode=="Camera")startCamera()
        publishOptions();updateSetupUi()
        val measured=core.snapshot(now())
        if(review!=null&&measured.speedValid&&measured.speed.isFinite()&&measured.speed in 0.0..1.0)
            showSessionReview()
    }
    private fun showSessionReview() {
        if(PracticeSessionReview.access(setup.isMonitoring(),knownMoving(),lastPracticeReview!=null,true)!=PracticeSessionReview.Access.READY)return
        val review=lastPracticeReview?:return
        presentSessionReviewDialog(android.app.AlertDialog.Builder(this).setTitle("Session review")
            .setMessage(review.text()).setPositiveButton("Done",null).create())
    }
    private fun openSessionReview() {
        val measured=core.snapshot(now())
        when(PracticeSessionReview.access(setup.isMonitoring(),knownMoving(),lastPracticeReview!=null,
            measured.speedValid&&measured.speed.isFinite()&&measured.speed in 0.0..1.0)) {
            PracticeSessionReview.Access.UNAVAILABLE->return
            PracticeSessionReview.Access.READY->showSessionReview()
            PracticeSessionReview.Access.CONFIRM_PARKED->presentSessionReviewDialog(android.app.AlertDialog.Builder(this)
                .setTitle("Review while parked").setMessage("Speed is unavailable. Park safely before reading your session review.")
                .setNegativeButton("Back",null).setPositiveButton("I'm parked") {_,_->showSessionReview()}.create())
        }
    }
    private fun presentSessionReviewDialog(dialog:android.app.AlertDialog) {
        sessionReviewDialog?.dismiss();sessionReviewDialog=dialog
        dialog.setOnDismissListener {if(sessionReviewDialog===dialog)sessionReviewDialog=null}
        dialog.show()
    }
    private fun updateSetupUi() {
        if(!::setupPanel.isInitialized||!::filmedVideo.isInitialized)return
        val monitoring=roadSource()&&setup.isMonitoring()
        val moving=knownMoving()
        if(monitoring||moving)sessionReviewDialog?.dismiss()
        dashboard.keepScreenOn=monitoring
        dashboard.setSessionRunning(monitoring)
        dashboard.sessionStatus.setUiText(if(monitoring)"Supervised practice" else if(roadSource())"Parked" else "Test input")
        val editable=resumed&&roadSource()&&!monitoring&&!moving
        destinationPanel.setRunning(resumed&&roadSource()&&!useGoogle)
        destinationPanel.setEditable(editable&&!useGoogle)
        googleNavigation?.setEditable(editable&&useGoogle)
        googleProviderButton.isEnabled=editable&&googleNavigation!=null
        offlineProviderButton.isEnabled=editable
        rightPane.visibility=if(monitoring)View.GONE else View.VISIBLE
        dashboard.showSetup(!monitoring&&showSetup)
        dashboard.showDestinationPage(showDestination)
        dashboard.settingsButton.isEnabled=!monitoring&&!moving
        dashboard.settingsButton.setUiText(if(showSetup&&!monitoring)"Close setup" else "Setup")
        dashboard.destinationButton.isEnabled=!monitoring&&!moving
        dashboard.prepareTab.isEnabled=editable
        dashboard.destinationTab.isEnabled=editable
        dashboard.quickStartButton.isEnabled=!monitoring&&!moving
        val canTouchMap=!monitoring&&!moving
        if(mapInteractive!=canTouchMap){mapInteractive=canTouchMap;activeMap.setInteractive(canTouchMap)}
        sessionEnd.visibility=if(monitoring)View.VISIBLE else View.GONE
        reviewSession.visibility=if(lastPracticeReview!=null&&!monitoring)View.VISIBLE else View.GONE
        reviewSession.isEnabled=editable
        setupPanel.visibility=if(mode=="Camera")View.VISIBLE else View.GONE
        labPanel.visibility=if(showLab||mode!="Camera")View.VISIBLE else View.GONE
        if(mode=="Camera"&&moving){labPanel.visibility=View.GONE;labToggle.isEnabled=false}else labToggle.isEnabled=true
        referenceSkip.visibility=if(setup.referencePending()||setup.referenceId()!=0L)View.VISIBLE else View.GONE
        labToggle.setUiText(if(showLab)"Hide lab settings" else "Lab settings and test sources")
        if(mode!="Camera")return
        val frame=lastCompletedVision?.takeIf {it.frame?.sourceId==generation.get().toLong()}
        val cameraFresh=frame!=null&&now()-frame.time in 0.0..<.35&&cameraRunning&&cameraError==null&&visionError==null&&cameraThermal<4
        dashboard.setReadiness(0,when {
            cameraError!=null||visionError!=null->"Unavailable · open setup options"
            !cameraRunning->"Starting the rear camera…"
            frame?.spatialReadiness==SpatialReadiness.MOUNT_UNSUPPORTED->"Point the rear camera towards the road"
            !cameraFresh->"Waiting for fresh camera evidence"
            frame?.spatialReadiness==SpatialReadiness.READY->"Forward view ready"
            else->"Basic detection · distance unavailable"
        },cameraFresh&&frame?.spatialReadiness!=SpatialReadiness.MOUNT_UNSUPPORTED)
        dashboard.setReadiness(1,if(!coachingSettings.voiceEnabled)"Voice off · visual coaching continues" else if(audio.ready)"Ready · tap Check sound" else "$audioStatus · visual coaching continues",!coachingSettings.voiceEnabled||audio.ready)
        val measured=core.snapshot(now())
        dashboard.setReadiness(2,if(measured.speedValid)"Measured speed available" else "No reliable speed yet",measured.speedValid)
        val alignment=frame?.let {SpatialReadiness.status(it.spatialReadiness)}?:"Waiting for camera analysis"
        val heading=if(setup.state()==DriveSetup.CHECK_MOUNT)"Check mount: roll changed while measured speed was near zero. Recheck while parked."
            else if(!roadSource())"Lab screen test selected. Select the road camera to set up a mounted session."
            else if(moving)"Setup paused: measured speed is above 1 km/h. Stop and park before setup."
            else "Confirm the fixed forward-facing mount before starting."
        setupStatus.setUiText("$heading\n$alignment\nCoaching: ${coachingSettings.profile.label} · voice ${if(coachingSettings.voiceEnabled)audioStatus else "off"}\n${setup.referenceStatus()}\nMount checks need fresh measured speed and cannot detect every movement.")
        // The entry action stays stable while frame age/capability changes. Its modal
        // explains readiness; both actual admission paths still enforce canStartRoad.
        setupStart.setUiText(if(!roadSource())"Use mounted road camera" else "Start session")
        setupStart.isEnabled=if(!roadSource())true else resumed&&!setup.isMonitoring()&&!moving
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
    private fun requestPermissions(includeLocation:Boolean=roadSource()) {
        val requested=mutableListOf(Manifest.permission.CAMERA)
        if(includeLocation)requested.addAll(listOf(Manifest.permission.ACCESS_FINE_LOCATION,Manifest.permission.ACCESS_COARSE_LOCATION))
        val wanted=requested.filter {ContextCompat.checkSelfPermission(this,it)!=PackageManager.PERMISSION_GRANTED}
        if(wanted.isNotEmpty())permissions.launch(wanted.toTypedArray()) else {sensors.start();if(mode=="Camera"&&!cameraRunning)startCamera()}
    }
    private fun publishOptions() {
        options=Options(mode,speed,pitch,tti,physical.isChecked,braking.isChecked,gear.isChecked,script,scriptStart,manualSpeed,slope.isChecked,demoGear,demoRpm,engineLoad.isChecked,filmedVideo.isChecked,videoPitch,phoneSlope.isChecked,
            ::researchEmergencies.isInitialized&&researchEmergencies.isChecked,generation.get().toLong())
    }
    private fun captureSource(next:String=mode)=when(next) {
        "Desk"->CaptureSource.SIMULATION;"MP4"->CaptureSource.RECORDING
        else->if(::filmedVideo.isInitialized&&filmedVideo.isChecked)CaptureSource.SCREEN else CaptureSource.CAMERA
    }
    private fun normalizePhone() {
        if(roadSource()) {
            if(setup.isMonitoring()||knownMoving())return
            if(!sensors.motionAvailable){status.text=sensors.motionStatus;return}
            android.app.AlertDialog.Builder(this).setTitle("Confirm level ground")
                .setMessage("Park on level ground with the phone fixed and rear camera forward. Confirm this location is level, then hold still for 5 seconds. The phone cannot verify level ground.")
                .setNegativeButton("Back",null).setPositiveButton("Parked on level ground") {_,_->
                    if(resumed&&roadSource()&&!knownMoving()&&!setup.isMonitoring())resetPhoneReference(true)
                }.show()
        } else resetPhoneReference(false)
    }
    private fun resetPhoneReference(confirmed:Boolean) {
        audio.cancel();setup.stop();practiceReview.discard();val previousId=core.calibrationId();core.reset();lastVision=null;lastCompletedVision=null
        if(confirmed)check(setup.requestLevelReference(previousId))
        if(resumed&&mode=="Camera")startCamera() else {beginSource(captureSource());publishOptions()}
    }
    private fun beginSource(kind:CaptureSource):Int {
        val token=generation.incrementAndGet()
        lastCompletedVision=null;lastSemanticVision=null
        val time=now()
        synchronized(core) {
            core.beginSource(token.toLong(),kind,time)
            // Automatic camera recovery also changes source. Retire current
            // PCM as well as native tickets before another onset can pass.
            if(::audio.isInitialized)audio.cancel()
        }
        journal?.offer(GuidanceTrace.source(token.toLong(),kind,time))
        return token
    }
    private fun loadOfflineRoute() {
        if(useGoogle||!::core.isInitialized||!::cameraFrame.isInitialized)return
        val file=java.io.File(filesDir,"navigation/active-route.json")
        val version=if(file.isFile)file.lastModified() else 0L
        if(version==routeFileVersion)return
        routeFileVersion=version;val token=++routeLoadGeneration;routeLoadNotice=null
        synchronized(routeMutationLock){loadedRoute=false;core.navigationClear()}
        evidenceIo.execute {
            val result=runCatching {
                if(!file.isFile)null else {
                    require(file.length()<=8*1024*1024)
                    val route=ai.learndrive.mobile.navigation.OfflineRoute.saved(file.readText())
                    val pack=ai.learndrive.mobile.navigation.OfflinePack.installed(this).firstOrNull {it.id==route.packId}
                        ?:error("Route map pack is missing")
                    synchronized(routeMutationLock) {
                        if(token!=routeLoadGeneration)return@runCatching null
                        check(core.navigationRoute(route.id,route.shape,route.maneuvers)){"Route geometry unsupported; map coaching paused"}
                    }
                    Pair(route,pack)
                }
            }
            main.post {
                if(isDestroyed||token!=routeLoadGeneration)return@post
                val data=result.getOrNull()
                if(data==null){loadedRoute=false;core.navigationClear();routeMap.showRoute(null);dashboard.setDestination(null)
                    routeLoadNotice=if(result.isFailure)"Offline route unavailable: ${result.exceptionOrNull()?.message}" else null;return@post}
                val (route,pack)=data
                loadedRoute=true
                routeInstructions=route.instructions
                availableMapName=pack.name
                routeMap.showPack(pack)
                routeMap.showRoute(ai.learndrive.mobile.navigation.DisplayRoute(route.id,route.shape))
                dashboard.setDestination(route.destinationLabel.ifBlank {"Saved destination"})
                destinationPanel.restore(route)
                routeStatus.text="Route context ready · camera and driving coach use this route"
                if(resumed)requestPermissions(includeLocation=true)
            }
        }
    }
    private fun clearOfflineRoute() {
        synchronized(routeMutationLock) {++routeLoadGeneration;loadedRoute=false;core.navigationClear()}
        java.io.File(filesDir,"navigation/active-route.json").delete();routeFileVersion=0L
        routeInstructions=emptyMap();routeLoadNotice=null;routeMap.showRoute(null);dashboard.setDestination(null);dashboard.setManeuver(null)
    }
    private fun switchMode(next:String) {
        levelStart.cancel();sessionStartDialog?.dismiss()
        setup.stop();practiceReview.discard()
        googleNavigation?.stopGuidance();coachCaption.clear()
        beginSource(captureSource(next));cameraRunning=false;videoRunning=false;player?.close();player=null;camera?.unbindAll();audio.cancel();lastVision=null;overlay.result=null
        visionError=null;cameraError=null;lastCameraFrameAt=0.0;cameraThermal=0
        mode=next;if(next=="Camera")script=null
        deskControls.visibility=if(next=="Desk")View.VISIBLE else View.GONE
        cameraAdviceControls.visibility=if(next=="Camera")View.VISIBLE else View.GONE
        publishOptions()
        prompt.text=if(next=="Desk")"Desk mode · simulated input" else "$next · waiting for vision"
        visionStatus=if(next=="Desk")"Desk: simulated inputs; lower TTI to 2 s for a warning" else "Loading camera / video"
        updateSetupUi()
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
    @androidx.annotation.OptIn(androidx.camera.camera2.interop.ExperimentalCamera2Interop::class)
    private fun startCamera(recoverSurface:Boolean=false) {
        // CameraX must receive an attached PreviewView surface. Starting from
        // onCreate/permission callbacks before the first layout can leave the
        // camera OPEN with no preview or analysis frames on some devices.
        if(!resumed||mode!="Camera")return
        if(!recoverSurface)cameraSurfaceRetries=0
        if(!preview.isAttachedToWindow||preview.width<=0||preview.height<=0) {
            if(!cameraAwaitingLayout) {
                cameraAwaitingLayout=true
                val waitGeneration=++cameraPreviewWaitGeneration
                val deadline=now()+3.0
                Log.d("LearnDriveVision","Waiting for camera preview: attached=${preview.isAttachedToWindow}, size=${preview.width}x${preview.height}")
                main.post(object:Runnable {
                    override fun run() {
                        if(!cameraAwaitingLayout||waitGeneration!=cameraPreviewWaitGeneration)return
                        if(!resumed||mode!="Camera"||isDestroyed){cameraAwaitingLayout=false;return}
                        if(preview.isAttachedToWindow&&preview.width>0&&preview.height>0) {
                            cameraAwaitingLayout=false
                            Log.d("LearnDriveVision","Camera preview ready: ${preview.width}x${preview.height}")
                            startCamera(recoverSurface)
                        } else if(now()>=deadline) {
                            cameraAwaitingLayout=false
                            cameraError="Camera preview did not become ready: attached=${preview.isAttachedToWindow}, size=${preview.width}x${preview.height}"
                            Log.e("LearnDriveVision",cameraError?:"Camera preview unavailable")
                        } else main.postDelayed(this,32)
                    }
                })
            }
            return
        }
        cameraAwaitingLayout=false
        ++cameraPreviewWaitGeneration
        // A view can still report isLayoutRequested inside its on-layout
        // callback. Waiting for another layout in that state can wait forever;
        // CameraX only needs this attached, nonzero-sized preview here.
        cameraRunning=false;lastVision=null;lastCompletedVision=null;lastSemanticVision=null
        SpatialVision.sensorIntrinsics=null
        SpatialVision.calibrationOrigin="unknown"
        if(ContextCompat.checkSelfPermission(this,Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED){cameraRunning=false;cameraError="Camera permission required in Android settings";return}
        visionError=null;cameraError=null;lastCameraFrameAt=0.0;cameraStartedAt=now()
        cameraRunning=false
        val token=beginSource(captureSource());publishOptions();val sourceKind=captureSource()
        val future=ProcessCameraProvider.getInstance(this)
        future.addListener({
            if(token!=generation.get()||!resumed)return@addListener
            try {
                camera=future.get();camera?.unbindAll()
                val rotation=preview.display?.rotation?:android.view.Surface.ROTATION_90
                val view=Preview.Builder().setTargetRotation(rotation).build().also {it.surfaceProvider=preview.surfaceProvider;cameraPreviewUseCase=it}
                val analysis=ImageAnalysis.Builder().setTargetRotation(rotation).setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST).build().also {cameraAnalysisUseCase=it}
                analysis.setAnalyzer(inference) {image->
                    try {
                        if(token==generation.get()&&resumed) {
                            lastCameraFrameAt=now()
                            val thermal=if(android.os.Build.VERSION.SDK_INT>=29)getSystemService(PowerManager::class.java).currentThermalStatus else 0
                            cameraThermal=thermal
                            if(CameraVisionHealth.shouldAnalyze(now(),lastRender,thermal)) {
                                analysisBusy=true;lastRender=now();val result=model().frame(image,token.toLong(),sourceKind) { semantic ->
                                    if(token==generation.get()){
                                        lastSemanticVision=PublishedSemantic(token.toLong(),semantic)
                                        lastVision=semantic;rules.post {evaluate()}
                                    }
                                }
                                if(token==generation.get()){visionError=null;lastCompletedVision=result;lastVision=result;recordFrame(result);rules.post {evaluate()}}
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
                runCatching {
                    val info=Camera2CameraInfo.from(bound.cameraInfo)
                    val intrinsics=info.getCameraCharacteristic(CameraCharacteristics.LENS_INTRINSIC_CALIBRATION)
                    val active=info.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE)
                    val physical=info.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE)
                    val focal=info.getCameraCharacteristic(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)?.firstOrNull()
                    if(active!=null) {
                        val fx=intrinsics?.getOrNull(0)?.toDouble()?:if(physical!=null&&focal!=null)focal*active.width()/physical.width.toDouble() else Double.NaN
                        val fyFull=intrinsics?.getOrNull(1)?.toDouble()?:if(physical!=null&&focal!=null)focal*active.height()/physical.height.toDouble() else Double.NaN
                        SpatialVision.sensorIntrinsics=doubleArrayOf(fx,fyFull,
                            (intrinsics?.getOrNull(2)?.toDouble()?:active.width()/2.0)-active.left,
                            (intrinsics?.getOrNull(3)?.toDouble()?:active.height()/2.0)-active.top,
                            active.width().toDouble(),active.height().toDouble())
                        SpatialVision.calibrationOrigin=if(intrinsics!=null)"camera metadata" else "estimated from lens metadata"
                    }
                    val fy=if(intrinsics!=null&&intrinsics.size>=2&&active!=null&&active.height()>0)
                        intrinsics[1].toDouble()*analysis.resolutionInfo!!.resolution.height/active.height() else Double.NaN
                    core.cameraIntrinsics(fy)
                }.onFailure {core.cameraIntrinsics(Double.NaN);Log.i("LearnDriveVision","Calibrated camera intrinsics unavailable; metric range disabled")}
                lastVision=null;lastCompletedVision=null;lastSemanticVision=null
                cameraRunning=true
                bound.cameraInfo.cameraState.removeObservers(this)
                bound.cameraInfo.cameraState.observe(this) {state->
                    if(token==generation.get()) {
                        cameraError=state.error?.let {"Camera error ${it.code}: ${it.cause?.message?:"camera stream unavailable"}"}
                        cameraRunning=state.type==androidx.camera.core.CameraState.Type.OPEN
                        if(state.error?.code==androidx.camera.core.CameraState.ERROR_STREAM_CONFIG&&
                            lastCameraFrameAt==0.0&&cameraSurfaceRetries<1) {
                            cameraSurfaceRetries++
                            main.postDelayed({
                                if(resumed&&mode=="Camera"&&token==generation.get()&&lastCameraFrameAt==0.0)
                                    startCamera(recoverSurface=true)
                            },300)
                        }
                        rules.post {evaluate()}
                    }
                }
                rules.post {evaluate()}
            }catch(e:Exception){cameraError=e.message?:e.javaClass.simpleName;Log.e("LearnDriveVision","Camera start failed",e)}
        },ContextCompat.getMainExecutor(this))
    }
    private fun startVideo(uri:android.net.Uri) {
        switchMode("MP4");val token=generation.get();val source=Mp4Source();player=source
        videoRunning=true;visionStatus="Recorded video: original calibration unavailable; semantic detections only"
        inference.execute {try {source.play(this,uri,model(),token.toLong()) {if(token==generation.get()){if(it.frame!=null)lastCompletedVision=it;lastVision=it;recordFrame(it);rules.post {evaluate()}}}}
            catch(e:Exception){if(token==generation.get())visionError=e.message?:e.javaClass.simpleName}
            finally {if(token==generation.get()){videoRunning=false;main.post {if(token==generation.get())visionStatus=
                visionError?.let {"MP4: $it"}?:"Recorded video stopped. Select MP4 to play again."}}}}
    }
    private fun evaluate() {
        if(!resumed)return
        val config=options
        val t=now();val measured=core.snapshot(t);val row=config.script?.at(t-config.scriptStart)
        val desk=config.mode=="Desk";val recording=config.mode=="MP4"
        val road=config.mode=="Camera"&&!config.filmedVideo
        val calibrationId=core.calibrationId()
        val levelAllowed=!road||setup.levelAllowed(calibrationId)
        val usePhysical=desk&&config.physical
        val telemetry=when {
            recording->Telemetry(t,config.manualSpeed,Double.NaN,Double.NaN,0.0,false,
                config.manualSpeed.isFinite()&&config.manualSpeed>=0,false,false,false,false)
            desk->Telemetry(t,row?.speed?:config.speed,if(usePhysical)measured.pitch else row?.pitch?:config.pitch,
                if(usePhysical)measured.acceleration else Double.NaN,measured.progress,
                !usePhysical||measured.calibrated,true,!usePhysical||measured.pitchValid,
                (row?.braking?:config.braking)||(usePhysical&&measured.braking),usePhysical&&measured.hardBraking,usePhysical&&measured.shock)
            config.filmedVideo&&config.phoneSlope->core.videoSpeedTelemetry(measured,config.manualSpeed)
            config.filmedVideo->core.videoTelemetry(measured,config.manualSpeed,config.videoPitch)
            // Ordinary road coaching never substitutes a lab speed for a missing measurement.
            else->measured
        }.let {if(!levelAllowed)it.copy(pitch=Double.NaN,pitchValid=false) else it}
        val speedSource=when {
            recording->"manual recording test setting"
            desk->"simulated"
            config.filmedVideo->"video test setting"
            measured.speedValid&&measured.speed.isFinite()&&measured.speed>=0->"GPS + IMU"
            else->"measured speed unavailable"
        }
        currentAdvice=AdviceDisplay(telemetry,speedSource)
        val mute=telemetry.braking||telemetry.hardBraking||telemetry.shock
        if(mute&&!suppressed){audio.cancel();main.post {coachCaption.clear()}}
        suppressed=mute
        val result=lastVision?.takeIf {it.frame?.sourceId==config.sourceId}
        val htime=if(desk)t else result?.time?:0.0
        val htti=if(desk)row?.tti?:config.tti else result?.tti?:Double.POSITIVE_INFINITY
        val confidence=if(desk)row?.confidence?:.9 else result?.confidence?:0.0
        val path=if(desk)row?.path?:true else result?.inPath?:false
        if(desk&&config.script==null)core.engineSample(t,config.demoGear,6,config.demoRpm,1200.0,3000.0,config.engineLoad)
        val active=when(config.mode){"Camera"->cameraRunning&&(!road||setup.isMonitoring());"MP4"->videoRunning;else->true}
        val captionGeneration=navigationCaptionGeneration.get()
        val (decision,routeContext,googleContext)=synchronized(routeMutationLock) {
            val selected=core.guidance(telemetry,t,config.sourceId,active,audio.ready,row?.gear?:config.gear,
                config.slope&&!recording&&levelAllowed,config.emergencies,htime,htti,confidence,path)
            // Keep provider identity with this exact context; a parked provider
            // switch must not relabel Google content as exportable offline data.
            Triple(selected,if(selected.origin==5)core.navigationContext(t) else null,useGoogle)
        }
        warningStatus=if(road&&!setup.isMonitoring())"Coaching paused · complete setup while parked" else "Guidance: ${core.guidanceReason()}"
        if(!desk)warningStatus+="\nRoad: ${result?.takeIf {t-it.time>=0&&t-it.time<.35}?.road?.status?:"unavailable"}"
        if(!config.emergencies)warningStatus+="\nExperimental emergency commands off"
        val key="${decision.sourceId}:${decision.settingsRevision}:${decision.prompt}:${decision.origin}:${decision.blocked}:${decision.semanticAvailable}:${decision.spatialAvailable}:${decision.suppress}:${decision.tactical}:${decision.reflex}:${decision.terrain}:${decision.camera}:${decision.actorId}:${decision.evidenceKind}:${config.emergencies}:${result?.spatialReadiness}:${setup.state()}:${setup.referenceId()}:$calibrationId"
        var decisionTraced=false
        fun emitDecision() {
            val capturing=journal?.isAccepting==true
            val trace=GuidanceTrace.decision(decision,t,config.mode,if(decision.origin==5)"measured_navigation_gps" else speedSource,result?.frame,
                if(decision.blocked==5)"stale source or invalid time" else core.guidanceReason(),config.emergencies,result?.spatialReadiness,
                if(road)setup.state() else null,if(road)setup.referenceId() else null,calibrationId,if(capturing)telemetry else null,if(capturing)measured else null,
                GuidanceTrace.navigationForExport(routeContext,googleContext),
                core.coachingDiagnosticsJson(t),vision?.performanceSnapshot)
            Log.i("LearnDriveGuidance",trace);journal?.offer(trace)
            traceKey=key;traceAt=t
            decisionTraced=true
        }
        if(key!=traceKey||t-traceAt>=1.0)emitDecision()
        val delivery=if(!road||setup.isMonitoring())core.deliverCoaching(decision,audio) else null
        if(delivery!=null) {
            // Audio can become available between sampled ticks. Preserve the
            // exact eligible decision before its acceptance acknowledgement.
            if(!decisionTraced)emitDecision()
            val acceptedAt=delivery.acceptedAt
            if(road&&setup.isMonitoring())practiceReview.accepted(acceptedAt,decision.origin,core.promptText(decision.prompt),delivery.voiceSubmitted)
            val presented=GuidanceTrace.coachingAccepted(decision,acceptedAt,delivery.voiceSubmitted)
            Log.i("LearnDriveGuidance",presented);journal?.offer(presented)
            if(delivery.voiceSubmitted) {
                val trace=GuidanceTrace.accepted(decision,acceptedAt);Log.i("LearnDriveGuidance",trace);journal?.offer(trace)
            }
            main.post {
                val currentContext=if(decision.origin==5&&useGoogle)core.navigationContext(now()) else null
                val currentEncounter=currentContext==null||(currentContext[10]!=0.0&&routeContext!=null&&
                    currentContext[0]==routeContext[0]&&currentContext[1]==routeContext[1])
                if(decision.sourceId==core.sourceId&&decision.settingsRevision==coachingSettings.revision&&currentEncounter&&
                    (decision.origin!=5||captionGeneration==navigationCaptionGeneration.get()))
                    coachCaption.accept(core.promptText(decision.prompt),decision.sourceId,acceptedAt,audio.durationSeconds(decision.prompt),routeContext,decision.origin)
            }
        }
    }
    private val ruleTick=object:Runnable {
        override fun run() {if(resumed){evaluate();rules.postDelayed(this,10)}}
    }
    private fun slopeLabel(degrees:Double)=when(core.slopeBand(degrees)) {
        1->"Gentle uphill";2->"Uphill";3->"Steep uphill";4->"Very steep uphill"
        -1->"Gentle downhill";-2->"Downhill";-3->"Steep downhill";-4->"Very steep downhill"
        else->"Near level"
    }
    private fun currentRoadOverlay(semantic:VisionResult?,spatial:VisionResult?):VisionResult? {
        if(semantic==null)return null
        if(overlaySemantic!==semantic||overlaySpatial!==spatial) {
            overlaySemantic=semantic;overlaySpatial=spatial
            roadOverlay=semantic.copy(road=spatial?.road)
        }
        return roadOverlay
    }
    private val tick=object:Runnable {
        override fun run() {
            if(!resumed)return
            publishOptions()
            val measured=core.snapshot(now());val display=currentAdvice;val telemetry=display?.telemetry?:measured
            if(roadSource()) {
                val t=now();val up=core.cameraUp(t)
                val fresh=up.all {it.isFinite()}&&up.sumOf {it*it}>.81
                val calibrationId=core.calibrationId();val previousReference=setup.referenceId()
                val checkMount=setup.update(t,calibrationId,measured.calibrated,fresh,up[0],up[1],up[2],measured.speed,measured.speedValid)
                if(previousReference!=0L&&!setup.levelAllowed(calibrationId)){audio.cancel();coachCaption.clear()}
                if(checkMount) {
                    practiceReview.discard()
                    audio.cancel();core.reset();startCamera();publishOptions()
                }
            }
            updateAutomaticLevelStart()
            val result=lastCompletedVision?.takeIf {it.frame?.sourceId==generation.get().toLong()}
            val htti=if(mode=="Desk")script?.at(now()-scriptStart)?.tti?:tti else result?.tti?:Double.POSITIVE_INFINITY
            val visionNow=now();val issue=if(mode=="Camera")cameraError?:visionError else null
            val live=result?.takeIf {CameraVisionHealth.canDisplay(visionNow,it.time,it.completedAt,if(mode=="Camera")cameraThermal else 0,issue)}
            val currentSource=generation.get().toLong()
            val semantic=lastSemanticVision?.takeIf {it.sourceId==currentSource}
            val freshSpatial=result?.takeIf {RoadCoachingStatus.currentEvidence(visionNow,currentSource,
                it.frame?.sourceId?:-1L,it.time,it.completedAt,cameraRunning,cameraThermal,issue)}
            val roadHealth=RoadCoachingStatus.assess(visionNow,currentSource,semantic?.sourceId?:-1L,
                semantic?.result?.time?:-1.0,semantic?.result?.completedAt?:-1.0,cameraRunning,audio.ready,
                cameraThermal,issue,semantic?.result?.spatialReadiness?:SpatialReadiness.ORIGINAL_CALIBRATION_UNKNOWN,
                freshSpatial?.spatialReadiness==SpatialReadiness.READY)
            if(roadSource()&&setup.isMonitoring())practiceReview.sample(visionNow,roadHealth.currentEvidence)
            // Lab playback may retain a visibly delayed diagnostic result. The
            // road view must never put old green boxes over the current preview.
            overlay.result=when {
                mode=="Desk"->null
                roadSource()->if(roadHealth.currentEvidence)currentRoadOverlay(semantic?.result,freshSpatial) else null
                else->live
            }
            if(mode=="Camera") {
                visionStatus=CameraVisionHealth.status(visionNow,result?.time?:-1.0,result?.completedAt?:-1.0,lastCameraFrameAt,
                    cameraStartedAt,result?.detections?.size?:0,cameraThermal,issue,analysisBusy)
                if(options.filmedVideo)visionStatus+="\nScreen test: original calibration unknown; semantic detections only"
                else if(live!=null)visionStatus+="\n${SpatialReadiness.status(live.spatialReadiness)}"
            } else if(mode=="MP4"&&live!=null&&videoRunning) {
                visionStatus="Recorded video: ${live.detections.size} detections. No original calibration or motion data."
            }
            if(mode=="Camera"&&live!=null&&prompt.text=="Camera · waiting for vision")prompt.text="Camera · live detections"
            // Diagnostic views are hidden during ordinary practice. Formatting
            // and replacing their text at 20 Hz only adds UI/layout work.
            if(showLab||mode!="Camera") {
            val speedText=if(telemetry.speedValid)"Advice %.1f km/h (%s)".format(telemetry.speed,display?.speedSource?:"starting") else "Advice speed unavailable (${display?.speedSource?:"starting"})"
            val approach=if(htti.isFinite())"%.1f s".format(htti) else "not estimated"
            metrics.text="$speedText   |   pitch %.1f°   |   TTI $approach\nCalibration %d%% · %s · %s\nVision %.1f ms · %s".format(telemetry.pitch,(measured.progress*100).toInt(),if(telemetry.braking)"braking" else "coasting",if(measured.shock)"tap/shock" else "quiet",result?.processingMs?:0.0,audioStatus)
            if(mode=="MP4")metrics.text="$speedText\nRecorded motion and calibration: unavailable\nTTI $approach · Vision %.1f ms · $audioStatus".format(result?.processingMs?:0.0)
            if(mode=="Camera") {
                val phone=core.inclination(now())
                val usingPhone=!options.filmedVideo||options.phoneSlope
                val levelAllowed=!roadSource()||setup.levelAllowed(core.calibrationId())
                val slopeText=if(usingPhone) {
                    if(!levelAllowed)setup.referenceStatus()
                    else if(phone.valid)"${slopeLabel(phone.degrees)} · estimated %+.1f° (%+.1f%%)".format(phone.degrees,phone.gradePercent)
                    else if(measured.calibrated)"Slope reading unavailable"
                    else "Phone slope unavailable; ${core.calibrationReason()}"
                } else "${slopeLabel(telemetry.pitch)} · simulated %+.1f° (%+.1f%%)".format(telemetry.pitch,100*kotlin.math.tan(telemetry.pitch*Math.PI/180))
                planeStatus.text=when {
                    roadSource()&&!levelAllowed->setup.referenceStatus()
                    !sensors.motionAvailable->sensors.motionStatus
                    !measured.calibrated->"Normalization ${(measured.progress*100).toInt()}% · ${core.calibrationReason()}"
                    !phone.valid->"Phone plane normalized · slope reading unavailable"
                    else->"Phone plane normalized · measured %+.1f° (%+.1f%%)".format(phone.degrees,phone.gradePercent)
                }
                val visionText="%.1f ms".format(result?.processingMs?:0.0)
                metrics.text=if(roadSource()&&setup.isMonitoring())"$speedText\n$slopeText" else "$speedText\n$slopeText\nTTI $approach · Vision $visionText · $audioStatus"
            }
            }
            status.setUiText(when {
                mode=="Desk"->"Simulated driving"
                mode=="MP4"||options.filmedVideo->"Recorded / screen test · basic detection"
                else->roadHealth.status
            })
            if(showLab||mode!="Camera")metrics.append("\n$visionStatus\n$warningStatus")
            dashboard.setMeasuredSpeed(if(roadSource()&&measured.speedValid)measured.speed else null,"GPS + IMU")
            dashboard.setMeasuredGear(null)
            val fix=sensors.latestPosition?.takeIf {roadSource()&&visionNow-it.elapsedRealtimeNanos/1e9 in 0.0..2.5&&it.hasAccuracy()&&it.accuracy<=25}
            activeMap.updatePosition(fix?.let {ai.learndrive.mobile.navigation.DisplayPosition(it.elapsedRealtimeNanos/1e9,it.latitude,it.longitude,it.accuracy.toDouble())})
            if(useGoogle)googleNavigation?.tick(visionNow) else destinationPanel.tick()
            if(useGoogle) {
                val google=googleNavigation
                synchronized(core) {
                    val validEvidence=core.navigationContext(visionNow)[10]!=0.0
                    if(!validEvidence) {
                        if(providerEvidenceAvailable)navigationCaptionGeneration.incrementAndGet()
                        coachCaption.clearNavigation();audio.cancelIfPrompt {it in 47..55}
                    }
                    providerEvidenceAvailable=validEvidence
                }
                val update=google?.lastUpdate?.takeIf {visionNow-it.receivedAtSeconds in 0.0..3.0}
                dashboard.navigationStatus.setUiText(if(!roadSource())"Navigation coaching paused for test input" else google?.state?.status?:"Google navigation unavailable")
                val valid=roadSource()&&fix!=null&&update?.state==GoogleNavigationPhase.ENROUTE
                dashboard.setManeuver(if(valid)googleManeuverText(requireNotNull(update)) else when {
                    !roadSource()->"Test input"
                    google?.state?.routeReady==true&&!google.state.guiding->"Route ready · start session"
                    update?.state==GoogleNavigationPhase.REROUTING->"Updating route"
                    google?.state?.guiding==true->"Waiting for reliable location"
                    else->null
                },if(valid)update?.distanceMeters else null)
            } else if(loadedRoute) {
                val navigation=core.navigationContext(visionNow)
                dashboard.navigationStatus.setUiText(if(roadSource())core.navigationStatus(visionNow) else "Map coaching paused for test input")
                val instruction=if(roadSource()&&navigation[10]!=0.0)routeInstructions[navigation[12].toInt()]?.takeIf {it.isNotBlank()} else null
                dashboard.setManeuver(instruction?:if(roadSource())"Waiting for route position" else "Test input",if(instruction!=null)navigation[13] else null)
                if(journal?.isAccepting==true&&now()-lastNavigationTrace>=1) {
                    lastNavigationTrace=now();journal?.offer(GuidanceTrace.navigation(core.navigationContext(lastNavigationTrace),lastNavigationTrace))
                }
            }else {
                dashboard.navigationStatus.setUiText(routeLoadNotice?:availableMapName?.let {"$it · offline region"}?:"Add an offline region in settings")
                dashboard.setManeuver(if(routeLoadNotice!=null)"Route unavailable" else null)
            }
            val captionActive=when(mode){"Camera"->cameraRunning&&(!roadSource()||setup.isMonitoring());"MP4"->videoRunning;else->true}
            if(mode=="Camera"&&issue!=null)coachCaption.clear()
            val caption=coachCaption.presentation(visionNow,core.sourceId,captionActive,
                if(useGoogle||loadedRoute)core.navigationContext(visionNow) else null)
            dashboard.setCoachPresentation(caption?.text?:when {
                    roadSource()&&!setup.isMonitoring()->"Your next session starts here."
                    mode=="Desk"->"Desk test · simulated driving"
                    mode=="MP4"->"Recorded video test"
                    options.filmedVideo->"Screen test · recorded scene only"
                    else->roadHealth.idleCaption
                },caption?.context?:if(roadSource()&&setup.isMonitoring())"Supervised practice · advisory cues" else "YOUR NEXT STEP")
            updateSetupUi()
            if(showLab||mode!="Camera")updateEvidenceUi()
            main.postDelayed(this,50)
        }
    }
    override fun onStart(){super.onStart();routeMap.onStart();googleNavigation?.onStart()}
    override fun onResume() {super.onResume();routeMap.onResume();googleNavigation?.onResume();resumed=true;loadOfflineRoute();if(::sensors.isInitialized)sensors.start();if(mode=="Camera"){inference.execute {visionFailure=null};startCamera()};main.post(tick);rules.post(ruleTick)}
    override fun onPause() {resumed=false;levelStart.cancel();sessionStartDialog?.dismiss();cameraAwaitingLayout=false;++cameraPreviewWaitGeneration;destinationPanel.setEditable(false);destinationPanel.setRunning(false);googleNavigation?.setEditable(false);googleNavigation?.stopGuidance();synchronized(evidenceLock){journal?.stop("backgrounded")};if(evidenceStartPending){evidenceRequest.incrementAndGet();evidenceStartPending=false;evidenceBusy=false};setup.stop();practiceReview.discard();cameraRunning=false;videoRunning=false;beginSource(captureSource());player?.close();camera?.unbindAll();sensors.close();audio.cancel();main.removeCallbacks(tick);rules.removeCallbacksAndMessages(null);routeMap.onPause();googleNavigation?.onPause();super.onPause()}
    override fun onStop(){routeMap.onStop();googleNavigation?.onStop();super.onStop()}
    override fun onSaveInstanceState(out:Bundle){routeMap.onSaveInstanceState(out);googleNavigation?.onSaveInstanceState(out);super.onSaveInstanceState(out)}
    override fun onLowMemory(){super.onLowMemory();if(::routeMap.isInitialized)routeMap.onLowMemory();googleNavigation?.onLowMemory()}
    override fun onDestroy() {getSystemService(android.hardware.display.DisplayManager::class.java).unregisterDisplayListener(displayListener);journal?.stop("activity_destroyed");googleNavigation?.close(isChangingConfigurations);googleNavigation=null;destinationPanel.close();routeMap.close();evidenceIo.shutdown();audio.close();sensors.close();player?.close();ruleThread.quitSafely();inference.execute {vision?.close();core.close()};inference.shutdown();main.removeCallbacksAndMessages(null);super.onDestroy()}
}
