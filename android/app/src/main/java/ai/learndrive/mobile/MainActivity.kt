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
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.core.Preview
import androidx.camera.core.UseCaseGroup
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.camera2.interop.Camera2CameraInfo
import androidx.camera.camera2.interop.Camera2CameraControl
import androidx.camera.camera2.interop.CaptureRequestOptions
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
import ai.learndrive.mobile.navigation.google.RouteElevation
import ai.learndrive.mobile.navigation.google.nativeCode
import ai.learndrive.mobile.navigation.google.focusTurn
import kotlin.math.roundToInt
import androidx.lifecycle.ViewModelProvider
import ai.learndrive.mobile.ui.CoachStyle
import ai.learndrive.mobile.ui.DriveCards
import ai.learndrive.mobile.ui.TurnCard

class MainActivity : ComponentActivity() {
    private lateinit var core:NativeCore
    private lateinit var learnedBumps:LearnedBumps
    // "You first": the learner's measured responses to road users, kept between drives.
    private lateinit var learnerRecord:LearnerRecordStore
    // Who is driving: each learner keeps their own record, practice records and coach settings.
    private lateinit var learners:Learners
    // While the native coach's record is swapped for the next learner's (on evidenceIo): no drive starts.
    @Volatile private var learnerSwitching=false
    private var learnerDialog:android.app.Dialog?=null
    // Parked (0.10.0): the sheet's "Get ready" page, first open's questions, and the Progress,
    // Settings and About pages (ParkedFlow, ui/ParkedUi).
    private lateinit var before:ai.learndrive.mobile.ui.BeforeYouDrive
    private var onboarding:ai.learndrive.mobile.ui.Onboarding?=null
    private var page:ai.learndrive.mobile.ui.Page?=null
    private var pageKind:String?=null
    /** "Who's driving?" has been asked since the last drive (asked only with two or more learners). */
    private var whoAsked=false
    private val developerUnlock=ParkedFlow.DeveloperUnlock()
    /** The phone's own flow state: first open done, developer tools shown. */
    private val flowPrefs by lazy {getSharedPreferences("app_flow",MODE_PRIVATE)}
    private var developerTools=false
    /** The car (shared by every learner): manual gears get gear advice; hill coaching on or off. */
    private var manualCar=true
    private var slopeGuidance=true
    // The start dialog's own start button, and whether this start takes its hills from the map.
    private var startButtonLabel="Level ground · start"
    private var startWithMapHills=false
    private var lastLearnerSummary:IntArray?=null
    private var lastBumpSave=0.0
    private lateinit var sensors:Sensors
    private lateinit var audio:AudioWorker
    private lateinit var pretrained:PretrainedShadow
    private lateinit var roads:RoadNetwork
    private lateinit var reasoningStatus:TextView
    private var lastReasoningSummary=Double.NEGATIVE_INFINITY
    private lateinit var learningRecords:LearningRecordStore
    private var lessonBaseline=org.json.JSONObject()
    private var lessonUiPhase="WATCHING_ROAD"
    private var drivingTask="OBSERVING"
    private var lastLessonUi=0.0
    /** The gear the control coach advises for the measured speed (0: none); never the gear the car is in. */
    private var advisedGear=0
    private var lastBatteryRead=-1e9
    private val modelImport=registerForActivityResult(ActivityResultContracts.OpenDocument()){uri->
        if(uri!=null&&::pretrained.isInitialized)pretrained.importModel(uri)
    }
    private lateinit var coachingPreferences:CoachingPreferences
    @Volatile private var coachingSettings=CoachingSettings()
    private lateinit var preview:PreviewView
    private lateinit var overlay:DetectionOverlay
    // How the analysed frame (its crop, rotated) lies on the preview, from CameraX itself: boxes
    // drawn through it sit on the road user whatever crop the camera ends up using (Nov 17: after a
    // camera stall every box was drawn ~20% too small and up-left of what it marked).
    @Volatile private var analysisTransform:androidx.camera.view.transform.OutputTransform?=null
    @androidx.annotation.OptIn(androidx.camera.view.TransformExperimental::class)
    private val analysisTransforms=androidx.camera.view.transform.ImageProxyTransformFactory().apply {isUsingCropRect=true;isUsingRotationDegrees=true}
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
    // Hills from the map: Google's elevation along the route (memory only, with the route).
    private var routeElevation:RouteElevation?=null
    @Volatile private var hillsDetail=RouteElevation.CHOOSE_TEXT
    @Volatile private var hillsState=RouteElevation.State.NO_ROUTE
    @Volatile private var hillsReady=false
    // Google's elevation cannot be read (no key, or the key may not use it): only then is the phone's tilt the fallback.
    @Volatile private var hillsUnavailable=false
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
    // One focus per drive (0.9.9, DriveFocus): chosen and briefed parked at the start, measured
    // during the drive, the review about it after.
    private class FocusBrief(val startedAt:Double) {var text:String?=null;var chosenAt=Double.NaN;var sayFrom=Double.NaN;var done=false}
    private var focusBrief:FocusBrief?=null
    private var focusSession:DriveFocus.Session?=null
    private var lastFocus:DriveFocus.Drive?=null
    /** The starting checks and moving off (StartRoutine), said one step at a time, parked. */
    private class Opening(val steps:List<StartRoutine.Step>) {
        var index=0
        var nextAt=Double.NEGATIVE_INFINITY
        var waitingSince=Double.NaN
        var shown=-1
        var shownUntil=Double.NEGATIVE_INFINITY
        var done=false
        /** Every check has been said (or shown): today's focus may follow. */
        val pastChecks get()=done||steps.getOrNull(index)?.part!=StartRoutine.Part.CHECKS
    }
    private var opening:Opening?=null
    /** The coach's voice: recorded (what it has no take of, the phone's voice says) or the phone's own voice. */
    private var voiceKind=RECORDED_VOICE
    // Sample of the phone's voice asked for by Hear it: said once rendered.
    private var pendingSample:String?=null
    private var pendingSampleSince=0.0
    private var focusRouteAt=Double.NEGATIVE_INFINITY
    private var focusCard:Pair<String,String>?=null
    private var focusCardUntil=Double.NEGATIVE_INFINITY
    /** Google's turns still to come on the route (from its steps): a count, nothing else. */
    private var googleTurnsAhead:Int?=null
    private lateinit var reviewSession:Button
    private var sessionReviewDialog:android.app.AlertDialog?=null
    private lateinit var physical:Switch
    private lateinit var braking:Switch
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
    @Volatile private var roadMapStatus="Road map: waiting for GPS"
    // Opt-in instructor voice recording for the instructor benchmark (with diagnostics only).
    private lateinit var lessonAudio:InstructorRecorder
    private lateinit var evidenceExportAudio:Button
    private var lessonAudioSaved=false
    private var lessonAudioPendingUntil=Double.NEGATIVE_INFINITY
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
    // The bound rear camera and the frame-rate cap applied to it (CameraVisionHealth.cameraFpsCap).
    private var boundCamera:androidx.camera.core.Camera?=null
    @Volatile private var appliedFpsCap=30
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
    private val CAMERA_SURFACE_RETRIES=3
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
    // The phone's own natural voice (offline), rendered once per phrase; see PhoneVoice.
    private lateinit var phoneVoice:ai.learndrive.mobile.audio.PhoneVoice
    @Volatile private var voiceStatus="Natural voice: starting"
    /** The coach's voice language (VoicePhrases): English, or Malayalam in the phone's own voice. */
    private var voiceLanguage=ai.learndrive.mobile.audio.VoicePhrases.ENGLISH
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
    private val exportLessonAudio=registerForActivityResult(ActivityResultContracts.CreateDocument("audio/mp4")) {uri->
        if(uri!=null) {
            evidenceBusy=true
            evidenceIo.execute {
                val message=runCatching {
                    check(journal?.isFinished!=false&&!lessonAudio.active){"Stop capture before export"}
                    requireNotNull(contentResolver.openOutputStream(uri,"w")).use {out->lessonAudio.file.inputStream().use {it.copyTo(out)}}
                    "Instructor audio exported. Use it with the diagnostics file in the instructor benchmark."
                }.getOrElse {"Export failed: ${it.message}"}
                main.post {evidenceBusy=false;evidenceNotice=message}
            }
        }
    }
    /** The coach's recorded voice built into the app for the voice language (null: none, the phone's voice speaks). */
    private var appVoice:ai.learndrive.mobile.audio.AppVoice?=null
    @Volatile private var appVoiceStatus=""
    // The permission dialog pauses the activity; start once resumed, and only shortly after consent.
    private val micPermission=registerForActivityResult(ActivityResultContracts.RequestPermission()) {granted->
        if(granted){lessonAudioPendingUntil=now()+30;main.post {startPendingLessonAudio()}}
        else evidenceNotice="Microphone permission declined; instructor audio not recorded"
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
        learnedBumps=LearnedBumps(filesDir,core);evidenceIo.execute {runCatching {learnedBumps.load()}}
        val learnerList=getSharedPreferences("learners",MODE_PRIVATE)
        learners=Learners(object:Learners.KeyValue {
            override fun get(key:String)=learnerList.getString(key,null)
            override fun put(key:String,value:String)=learnerList.edit().putString(key,value).apply()
            override fun remove(key:String)=learnerList.edit().remove(key).apply()
        })
        val record=LearnerRecordStore(learnerFile(),core);learnerRecord=record;evidenceIo.execute {runCatching {record.load()}}
        core.learnerWaiting(learnerSettings().getBoolean("youFirst",true))
        // Junction-ahead coaching without a destination (online OpenStreetMap roads).
        roads=RoadNetwork(core) {message->android.util.Log.i("LearnDriveRoads",message);roadMapStatus=message};sensors.onFix={lat,lon->roads.onLocation(lat,lon)}
        coachingPreferences=CoachingPreferences(learnerSettings())
        coachingSettings=coachingPreferences.load();core.configureCoaching(coachingSettings);core.automaticCoaching(true)
        pretrained=PretrainedShadow(this,core){message->main.post {if(::reasoningStatus.isInitialized&&!isDestroyed)reasoningStatus.setUiText(message)}}
        // Fill the road camera panel, aligned to the bottom: a wide panel crops sky, never the road
        // (DetectionOverlay draws boxes with the same mapping).
        preview=PreviewView(this).apply {scaleType=PreviewView.ScaleType.FILL_END;implementationMode=PreviewView.ImplementationMode.COMPATIBLE}
        overlay=DetectionOverlay(this)
        cameraFrame=FrameLayout(this).apply {addView(preview,FrameLayout.LayoutParams(-1,-1));addView(this@MainActivity.overlay,FrameLayout.LayoutParams(-1,-1))}
        dashboard=ai.learndrive.mobile.ui.UnifiedDriveDashboard(this,cameraFrame)
        val outer=dashboard
        routeMap=ai.learndrive.mobile.navigation.OfflineNavigationMapSurface(this,saved)
        dashboard.attachMap(routeMap.view)
        prompt=dashboard.coachText;metrics=dashboard.detailsText;status=dashboard.roadStatus;routeStatus=dashboard.maneuverText
        sessionEnd=button("End") {endRoadSession()}.apply {visibility=View.GONE;CoachStyle.styleEnd(this);contentDescription="End session"}
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
            val metadata=packageManager.getApplicationInfo(packageName,PackageManager.GET_META_DATA).metaData
            val key=metadata?.getString("com.google.android.geo.API_KEY").orEmpty()
            // A separate Elevation API key if one is set up, else the Maps key.
            routeElevation=RouteElevation(this,metadata?.getString("ai.learndrive.ELEVATION_API_KEY").orEmpty().trim().ifBlank {key},routeMutationLock,
                apply={heights->if(core.routeElevation(heights))core.routeElevationCover()[3].toInt() else -1},
                status={state,detail->main.post {hillsDetail=detail;hillsState=state;hillsReady=state==RouteElevation.State.READY
                    hillsUnavailable=state==RouteElevation.State.DENIED||state==RouteElevation.State.UNAVAILABLE;updateSetupUi()}})
            googleNavigation=GoogleNavigationController(this,key,
                onState={if(useGoogle&&!isDestroyed){
                    dashboard.setDestination(it.destinationLabel.takeIf(String::isNotBlank))
                    if(it.routing&&destinationSelectionPending)destinationRouting=true
                    else if(!it.routing&&destinationRouting){destinationRouting=false;destinationSelectionPending=false;if(it.routeReady&&showDestination&&::setupStart.isInitialized){showDestination=false;updateSetupUi()}}
                }},
                onUpdate={if(useGoogle&&!isDestroyed)acceptGoogleNavigation(it)},
                journey=ViewModelProvider(this)[GoogleJourneyMemory::class.java],
                onDestinationSelected={destinationSelectionPending=true},
                onRouteLine={line->if(!isDestroyed)synchronized(routeMutationLock){
                    val loaded=core.routeLine(if(useGoogle)line else null)
                    routeElevation?.route(if(useGoogle&&loaded&&line!=null)line else null)}})
        }
        useGoogle=getSharedPreferences("navigation_provider",MODE_PRIVATE).getBoolean("google",true)&&googleNavigation!=null
        showNavigationProvider()
        // Get ready (0.10.0): what a learner chooses before every drive, all on show (the coaching
        // level, the coach's voice, today's focus), then whether the phone is ready, then the way to
        // Progress, Settings and About. Who is driving sits at the top of the sheet.
        learningRecords=LearningRecordStore(learnerRecords())
        // The coach's voice language. Malayalam is spoken by the phone's own Google voice for it,
        // rendered once on the phone; the screen stays in English.
        voiceLanguage=storedVoiceLanguage(learnerSettings())
        voiceKind=storedVoiceKind(learnerSettings())
        before=ai.learndrive.mobile.ui.BeforeYouDrive(this,ParkedFlow.LEVELS.map {it.label},
            VOICE_CHOICES.map {it.third},FOCUS_CHOICES.map {it.second},
            onLevel={i->chooseLevel(ParkedFlow.LEVELS[i])},
            // Never mid-drive: one drive keeps one voice.
            onVoice={i->setVoice(i)},
            onFocus={i->learnerSettings().edit().putString("driveFocus",FOCUS_CHOICES.getOrElse(i) {FOCUS_CHOICES[0]}.first).apply();updateSetupUi()})
        before.hear.setOnClickListener {hearVoice()}
        before.getVoice.setOnClickListener {installVoiceData()}
        before.progress.setOnClickListener {openPage("progress")}
        before.settings.setOnClickListener {openPage("settings")}
        before.about.setOnClickListener {openPage("about")}
        before.checks.addView(dashboard.readiness)
        setupPanel.addView(before)
        dashboard.learnerChip.setOnClickListener {chooseLearner()}
        dashboard.onReadiness={i->readinessTapped(i)}
        setupStart=button("Start drive") {confirmRoadSession()};CoachStyle.styleButton(setupStart,true)
        dashboard.setupActionHost.orientation=LinearLayout.HORIZONTAL
        reviewSession=button("Last drive") {openSessionReview()}.apply {visibility=View.GONE}
        dashboard.setupActionHost.addView(reviewSession,LinearLayout.LayoutParams(0,CoachStyle.dp(this,52),1f).apply {marginEnd=CoachStyle.dp(this@MainActivity,6)})
        dashboard.setupActionHost.addView(setupStart,LinearLayout.LayoutParams(0,CoachStyle.dp(this,52),1.5f))
        // Developer tools (test sources, diagnostics, experiments) stay out of a learner's way: shown
        // only after the version in About is tapped seven times, or while a test source runs.
        developerTools=flowPrefs.getBoolean("developer",false)
        labToggle=button("Developer tools") {showLab=!showLab;updateSetupUi()};sidebar.addView(labToggle)
        val controls=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL;visibility=View.GONE};labPanel=controls;sidebar.addView(controls)
        metrics.visibility=View.VISIBLE;controls.addView(metrics)
        setupStatus=text("Checking camera, motion and voice",12f).apply {setTextColor(CoachStyle.muted)};controls.addView(setupStatus)
        reasoningStatus=text("Local pretrained AI is experimental, optional and off. It never speaks or controls live guidance.",12f)
        controls.addView(reasoningStatus)
        val shadowToggle=Switch(this).apply {
            tag="pretrained-shadow";text="Experimental local AI shadow";setTextColor(CoachStyle.text);isChecked=false
            isEnabled=BuildConfig.PRETRAINED_RUNTIME_AVAILABLE
            setOnCheckedChangeListener {_,enabled->
                if(enabled&&(setup.isMonitoring()||knownMoving()))isChecked=false
                else pretrained.setEnabled(enabled)
            }
        }
        controls.addView(shadowToggle)
        controls.addView(button("Import pretrained GGUF model") {
            if(!setup.isMonitoring()&&!knownMoving()){shadowToggle.isChecked=false;modelImport.launch(arrayOf("*/*"))}
        })
        evidenceFile=java.io.File(filesDir,"diagnostics/session.jsonl")
        lessonAudio=InstructorRecorder(this,java.io.File(filesDir,"diagnostics/instructor.m4a"))
        evidenceStatus=text(evidenceNotice,13f);controls.addView(evidenceStatus)
        evidenceStart=button("Start local diagnostics") {startEvidence()};controls.addView(evidenceStart)
        evidenceStop=button("Stop diagnostics") {stopEvidence("user_stop")};controls.addView(evidenceStop)
        evidenceExport=button("Export diagnostics (JSONL)") {if(!evidenceBusy&&evidenceSaved&&journal?.isFinished!=false)exportEvidence.launch("LearnDrive-session.jsonl")};controls.addView(evidenceExport)
        evidenceExportAudio=button("Export instructor audio (M4A)") {if(!evidenceBusy&&lessonAudioSaved&&journal?.isFinished!=false&&!lessonAudio.active)exportLessonAudio.launch("LearnDrive-instructor.m4a")};controls.addView(evidenceExportAudio)
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
                    put("instructor_audio","separate opt-in M4A file; instructor_audio_start gives its start on this journal clock")
                    put("recording_sidecar_import_supported",false)
                }.toString()
            }
            val exists=evidenceFile.isFile||lessonAudio.file.isFile;val audioExists=lessonAudio.file.isFile
            main.post {evidenceMetadata=metadata.getOrNull();evidenceSaved=exists;lessonAudioSaved=audioExists;evidenceBusy=false
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
        // The car's own settings, shared by every learner (Settings › Your car, and first open).
        manualCar=preferences.getBoolean("manualVehicle",true);slopeGuidance=preferences.getBoolean("slopeGuidance",true)
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
            setup.stop();finishPractice(true)
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
        hideStatusBar()
        preferSixtyHertz()
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
            when {onboarding!=null->{if(onboarding?.back()!=true)finish()}
                page!=null->closePage()
                showDestination->{hideKeyboard();showDestination=false;updateSetupUi()}
                showLab->{showLab=false;updateSetupUi()}
                showSetup->{hideKeyboard();showSetup=false;updateSetupUi()}
                else->finish()}
        }})
        phoneVoice=newPhoneVoice()
        appVoice=newAppVoice()
        audio=AudioWorker(this,core::promptMayInterrupt,core.promptCount(),core::promptPriority,
            fragmentCount=core.fragmentCount(),fragments=core::promptFragments,fragmentText=core::fragmentText,
            promptText=core::promptText,voice=phoneVoice) {message->main.post {audioStatus=message}}
        // The voice is chosen whole now (recorded or the phone's, Get ready): the phone's English voice is always allowed.
        audio.naturalVoice=true
        requireVoicePhrases();applyVoice()
        audio.setVoiceEnabled(coachingSettings.voiceEnabled)
        core.bindAudioDispatcher(ai.learndrive.mobile.audio.AudioDispatcher(audio))
        researchEmergencies=Switch(this).apply {text="Lab only: unvalidated emergency commands";isChecked=false}
        controls.addView(researchEmergencies)
        researchEmergencies.setOnCheckedChangeListener {_,_->audio.cancel();publishOptions()}
        // First open: the questions, and the camera and location asked for where they are explained
        // (mounting the phone), not on launch. Afterwards, as before.
        val onboarded=flowPrefs.getBoolean("onboarded",false)
        if(onboarded||permissionsGranted(false))requestPermissions()
        if(!onboarded)showOnboarding()
        if(useGoogle)googleNavigation?.resumeIfPreviouslyAccepted()
        evidenceIo.execute {
            val pack=runCatching {ai.learndrive.mobile.navigation.OfflinePack.installed(this)
                .sortedWith(compareByDescending<ai.learndrive.mobile.navigation.OfflinePack>{it.metadata.optInt("places_schema",1)}.thenByDescending {it.id}).firstOrNull()}.getOrNull()
            main.post {if(!isDestroyed&&!loadedRoute){availableMapName=pack?.name;routeMap.showPack(pack)}}
        }
    }
    private fun roadSource()=mode=="Camera"&&!filmedVideo.isChecked
    private var phoneVoiceGeneration=0
    /** The phone voice the learner picked for [voiceLanguage] in Settings (null: the app's own choice). */
    private var phoneVoiceChosen:String?=null
    private fun preferredPhoneVoice(id:String=learners.current.id,language:String=voiceLanguage)=learnerSettings(id).getString("phoneVoice.$language",null)
    private fun newPhoneVoice():ai.learndrive.mobile.audio.PhoneVoice {
        // A voice already replaced (language change) must not overwrite the new one's status.
        val generation=++phoneVoiceGeneration
        phoneVoiceChosen=preferredPhoneVoice()
        return ai.learndrive.mobile.audio.PhoneVoice(this,voiceLanguage,phoneVoiceChosen) {message->main.post {if(generation==phoneVoiceGeneration)voiceStatus=message}}
    }
    private var appVoiceGeneration=0
    /** The recorded voice built into the app for [voiceLanguage]: unpacked once in the background,
     * then the coach speaks with it. */
    private fun newAppVoice():ai.learndrive.mobile.audio.AppVoice? {
        val generation=++appVoiceGeneration
        return ai.learndrive.mobile.audio.AppVoice.load(this,voiceLanguage) {message->main.post {
            if(generation==appVoiceGeneration&&!isDestroyed){appVoiceStatus=message;applyVoice();updateSetupUi()}
        }}
    }
    /** Every distinct phrase and sentence the coach can say in [language] (a recorded voice needs each). */
    private fun voiceScript(language:String=voiceLanguage)=ai.learndrive.mobile.audio.VoiceScript.lines(language,(1..core.fragmentCount()).map {core.fragmentText(it)},(1..core.promptCount()).map {core.promptText(it)})
    /** Every phrase the coach can say, in the voice language, so a voice takes over only when it has all of them. */
    private fun requireVoicePhrases() {
        val english=(1..core.promptCount()).map {core.promptText(it)}+(1..core.fragmentCount()).map {core.fragmentText(it)}
        phoneVoice.require(english.mapNotNull {ai.learndrive.mobile.audio.VoicePhrases.spoken(voiceLanguage,it)})
        appVoice?.require(voiceScript().map {it.words})
        // The starting checks and moving off: rendered ahead (and kept), not waited for.
        StartRoutine.lines(voiceLanguage).forEach {phoneVoice.clip(it)}
    }
    /** The recorded voice (when chosen) first: a phrase it has no take of is said by the phone's own
     * voice. The phone's voice alone when chosen. Until the phone's voice is ready, the bundled
     * English recordings speak. */
    private fun applyVoice() {
        if(!::audio.isInitialized)return
        audio.useVoices(listOfNotNull(appVoice?.takeIf {it.complete&&voiceKind==RECORDED_VOICE},phoneVoice))
    }
    /** A new phone voice for [voiceLanguage]; the old one stops. Until the new one has rendered
     * every phrase, the bundled English recordings speak. */
    private fun restartPhoneVoice() {
        if(!::audio.isInitialized||!::phoneVoice.isInitialized)return
        val old=phoneVoice
        voiceStatus="${ai.learndrive.mobile.audio.VoicePhrases.name(voiceLanguage)} voice: starting"
        phoneVoice=newPhoneVoice();old.close()
        appVoice?.close();appVoice=newAppVoice()
        requireVoicePhrases();applyVoice()
    }
    private fun setVoiceLanguage(language:String) {
        if(language==voiceLanguage||!ai.learndrive.mobile.audio.VoicePhrases.supported(language))return
        voiceLanguage=language
        learnerSettings().edit().putString("voiceLanguage",language).apply()
        restartPhoneVoice();updateSetupUi()
    }
    private fun storedVoiceLanguage(settings:android.content.SharedPreferences)=settings.getString("voiceLanguage",null)
        ?.takeIf(ai.learndrive.mobile.audio.VoicePhrases::supported)?:ai.learndrive.mobile.audio.VoicePhrases.ENGLISH
    /** Google's own screen for adding voice data (Malayalam is one of its downloadable voices). */
    private fun installVoiceData() {
        val google=android.content.Intent(android.speech.tts.TextToSpeech.Engine.ACTION_INSTALL_TTS_DATA).setPackage("com.google.android.tts")
        val opened=runCatching {startActivity(google)}.isSuccess||runCatching {startActivity(android.content.Intent("com.android.settings.TTS_SETTINGS"))}.isSuccess
        if(!opened){voiceStatus="Open Android Settings › Text-to-speech to add the Malayalam voice";updateSetupUi()}
    }
    private fun changeCoachingSettings(profile:CoachingProfile,voice:Boolean) {
        if(coachingSettings.profile==profile&&coachingSettings.voiceEnabled==voice)return
        applyCoachingSettings(profile,voice,coachingSettings.revision)
    }
    /** Coaching settings in force from now, saved for the learner driving. The revision rises past
     * both [after] and the one in force, so a decision made under earlier settings never matches. */
    private fun applyCoachingSettings(profile:CoachingProfile,voice:Boolean,after:Long) {
        synchronized(core) {
            val updated=CoachingSettings(profile,voice,Math.addExact(maxOf(after,coachingSettings.revision),1))
            core.configureCoaching(updated);coachingSettings=updated
            // Same lock order as native delivery/onset: core, then audio.
            if(::audio.isInitialized){audio.cancel();audio.setVoiceEnabled(voice)}
            coachingPreferences.save(updated)
        }
        if(::pretrained.isInitialized)pretrained.cancel()
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
        // Which of Google's turns the caption on screen belongs to. Google's feed blinks to
        // "rerouting" (or goes quiet for a moment) whenever it replaces or merely refreshes the
        // route: the turn on screen stays until the new route's turn is known, as the spoken
        // sentence does (see ProviderTurnCoach).
        val key=update?.let {when(it.state) {
            GoogleNavigationPhase.ENROUTE->"${it.sessionId}:${it.stepNumber}:${it.maneuver}:${it.roundaboutExit}"
            GoogleNavigationPhase.REROUTING,GoogleNavigationPhase.UNKNOWN->googleEventKey
            else->"${it.sessionId}:${it.state}"
        }}
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
            update.maneuversAheadMeters?.let {core.routeManeuvers(update.receivedAtSeconds,it)}
            // Side, roundabout exit and the step after this one: "In 100 metres, keep left",
            // "take the second exit", "Then turn left straight after".
            core.navigationStepDetail(update.receivedAtSeconds,update.side,update.roundaboutExit?:0,
                update.nextManeuver.nativeCode(),update.nextGapMeters?:Double.POSITIVE_INFINITY)
        }
        // The next stretch of the route's elevation before the car gets there.
        routeElevation?.progress(core.routeElevationCover())
        // Google's turns still to come, and its current step for a drive focused on turns.
        if(update.state==GoogleNavigationPhase.ENROUTE) {
            update.turnsAhead?.let {googleTurnsAhead=it}
            focusSession?.turn(update.stepNumber,update.maneuver.focusTurn(),update.distanceMeters,update.receivedAtSeconds)
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
        else->"Follow the route"
    }
    /** What the learner should know about the road ahead, for the coach card between instructions. */
    @Volatile private var nextRoadSummary:String?=null
    private fun distanceText(m:Double)=when {m>=1000->"%.1f km".format(m/1000);m>=20->"${(m/10).roundToInt()*10} m";else->"here"}
    private fun startEvidence() {
        if(evidenceBusy||evidenceSaved||journal?.isFinished==false||knownMoving()||evidenceMetadata==null)return
        android.app.AlertDialog.Builder(this).setTitle("Record local diagnostic evidence?")
            .setMessage("Stores device/model versions, frame geometry, sampled detections and motion estimates, decisions and accepted audio submissions. No images, video, microphone audio or GPS coordinates. One file up to 8 MiB; stops on backgrounding. Export or delete it before another capture.")
            .setNegativeButton("Back",null)
            .setNeutralButton("With instructor audio") {_,_->confirmLessonAudio()}
            .setPositiveButton("Start diagnostics") {_,_->beginEvidence(false)}.show()
    }
    private fun confirmLessonAudio() {
        android.app.AlertDialog.Builder(this).setTitle("Also record your instructor's voice?")
            .setMessage("For the instructor benchmark: the microphone records what your driving instructor says during this lesson, so the app's instructions can be compared with theirs afterwards. It also records the app's voice and anything else said in the car. Everyone in the car must agree. The audio stays on this phone (up to about 3.5 hours) until you export or delete it with the diagnostics, and recording stops when the app leaves the screen.")
            .setNegativeButton("Back",null).setPositiveButton("Everyone agrees") {_,_->
                if(checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)==android.content.pm.PackageManager.PERMISSION_GRANTED)beginEvidence(true)
                else micPermission.launch(android.Manifest.permission.RECORD_AUDIO)
            }.show()
    }
    private fun startPendingLessonAudio() {
        if(resumed&&now()<=lessonAudioPendingUntil){lessonAudioPendingUntil=Double.NEGATIVE_INFINITY;beginEvidence(true)}
    }
    private fun beginEvidence(withAudio:Boolean) {
        if(!resumed||evidenceBusy||evidenceSaved||knownMoving()||journal?.isFinished==false||evidenceMetadata==null)return
        val metadata=org.json.JSONObject(evidenceMetadata!!).apply {put("started_utc",java.time.Instant.now().toString());put("started_monotonic",now());put("instructor_audio_requested",withAudio)}.toString()
        evidenceBusy=true;evidenceStartPending=true;evidenceFinishedObserved=false
        val request=evidenceRequest.incrementAndGet()
        rules.post {
            var startError:String?=null;var audioError:String?=null
            synchronized(evidenceLock) {
                if(resumed&&request==evidenceRequest.get()) {
                    runCatching {
                        journal=SessionJournal(evidenceFile,metadata);traceKey="";traceAt=0.0
                        val config=options
                        val source=when(config.mode){"Desk"->CaptureSource.SIMULATION;"MP4"->CaptureSource.RECORDING;else->if(config.filmedVideo)CaptureSource.SCREEN else CaptureSource.CAMERA}
                        journal?.offer(GuidanceTrace.source(core.sourceId,source,now()))
                    }.onFailure {startError="Could not start diagnostics: ${it.message}"}
                    // Same clock as the journal: the benchmark aligns the instructor's words to the app's instructions.
                    if(withAudio&&startError==null)runCatching {
                        val start=lessonAudio.start {now()}
                        journal?.offer(GuidanceTrace.instructorAudioStart(start.time,start.uncertaintySeconds))
                    }.onFailure {audioError="Instructor audio not recorded: ${it.message}"}
                }
            }
            main.post {if(request==evidenceRequest.get()){evidenceStartPending=false;evidenceBusy=false
                evidenceNotice=startError?:audioError?:if(withAudio)"Diagnostics and instructor audio recording" else "Diagnostics enabled for this foreground interval"}}
        }
    }
    private fun stopLessonAudio(reason:String) {
        synchronized(evidenceLock) {
            if(!::lessonAudio.isInitialized||!lessonAudio.active)return
            val limit=lessonAudio.sizeLimitReached;val kept=lessonAudio.stop()
            journal?.offer(GuidanceTrace.instructorAudioStop(now(),if(limit)"size_limit" else reason,kept))
        }
    }
    private fun stopEvidence(reason:String) {
        synchronized(evidenceLock){stopLessonAudio(reason);journal?.stop(reason)}
    }
    private fun deleteEvidence() {
        if(evidenceBusy||journal?.isFinished==false||knownMoving())return
        android.app.AlertDialog.Builder(this).setTitle("Delete local diagnostics?")
            .setMessage("Deletes the app's saved diagnostic file and any instructor audio. Exported copies remain where you saved them.")
            .setNegativeButton("Keep",null).setPositiveButton("Delete") {_,_->
                if(evidenceBusy||journal?.isFinished==false)return@setPositiveButton
                evidenceBusy=true
                evidenceIo.execute {
                    val deleted=(!evidenceFile.exists()||evidenceFile.delete())&&(!lessonAudio.file.exists()||lessonAudio.file.delete())
                    main.post {evidenceBusy=false;if(deleted){journal=null;evidenceSaved=false;lessonAudioSaved=false;evidenceNotice="Local diagnostics deleted"}else evidenceNotice="Could not delete local diagnostics"}
                }
            }.show()
    }
    private fun recordFrame(result:VisionResult) {
        val target=journal?:return;if(!target.isAccepting)return
        if(frameJournalId!=target.sessionId){frameJournalId=target.sessionId;frameJournalSource=-1;frameJournalTime=null}
        if(frameJournalSource!=result.frame?.sourceId||frameJournalTime!=result.semanticCaptureTime) {
            runCatching {target.offer(GuidanceTrace.frame(result,cameraThermal,appliedFpsCap))}.onFailure {target.recordLoss();Log.w("LearnDriveEvidence","Could not serialize frame evidence",it)}
            frameJournalSource=result.frame?.sourceId?:-1;frameJournalTime=result.semanticCaptureTime
        }
    }
    private fun updateEvidenceUi() {
        val target=journal
        // Instructor audio never outlives its diagnostics (the alignment lives in the journal).
        if(lessonAudio.active&&(target?.isAccepting!=true))stopLessonAudio("diagnostics_stopped")
        else if(lessonAudio.active&&lessonAudio.sizeLimitReached)stopLessonAudio("size_limit")
        if(target?.isFinished==true&&!evidenceFinishedObserved&&!evidenceBusy) {
            evidenceFinishedObserved=true;evidenceBusy=true
            evidenceIo.execute {val exists=evidenceFile.isFile;val audio=lessonAudio.file.isFile
                main.post {evidenceSaved=exists||audio;lessonAudioSaved=audio;evidenceBusy=false;evidenceNotice=if(exists)"Saved locally; export or delete before a new capture" else "No diagnostic file was saved"}}
        }
        evidenceStatus.text=if(target!=null)"${target.status()}\n$evidenceNotice" else evidenceNotice
        evidenceStart.isEnabled=!evidenceBusy&&!evidenceSaved&&target?.isFinished!=false&&evidenceMetadata!=null&&!knownMoving()
        evidenceStop.isEnabled=target?.isAccepting==true
        evidenceExport.isEnabled=!evidenceBusy&&evidenceSaved&&target?.isFinished!=false
        evidenceExportAudio.isEnabled=!evidenceBusy&&lessonAudioSaved&&target?.isFinished!=false&&!lessonAudio.active
        evidenceExportAudio.visibility=if(lessonAudioSaved||lessonAudio.active)View.VISIBLE else View.GONE
        evidenceDelete.isEnabled=!evidenceBusy&&evidenceSaved&&target?.isFinished!=false&&!knownMoving()
    }
    private val movingLock=MovingLock()
    /** The car is clearly moving: parked-only controls are locked. Speed noise while parked never counts. */
    private fun knownMoving():Boolean {
        val t=now();val measured=core.snapshot(t)
        return movingLock.update(t,measured.speedValid,measured.speed)
    }
    private fun canStartRoad():Boolean=canStartRoad(knownMoving())
    private fun canStartRoad(moving:Boolean):Boolean {
        if(!roadSource()||setup.isMonitoring()||setup.referencePending()||!cameraRunning||moving||learnerSwitching)return false
        val frame=lastCompletedVision?:return false;val age=now()-frame.time
        return frame.frame?.sourceId==generation.get().toLong()&&age>=0&&age<.35&&visionError==null&&cameraError==null&&!CameraVisionHealth.paused(cameraThermal)&&
            frame.spatialReadiness!=SpatialReadiness.MOUNT_UNSUPPORTED
    }
    private fun confirmRoadSession() {
        if(!roadSource()) {
            if(mode!="Camera")switchMode("Camera")
            filmedVideo.isChecked=false;requestPermissions();return
        }
        if(!resumed||knownMoving()||setup.isMonitoring()||learnerSwitching){updateSetupUi();return}
        if(sessionStartDialog?.isShowing==true)return
        // The subscription is asked about here only, parked, before a drive: never during one.
        val access=driveAccess()
        if(!access.allowed){showSubscriptionNeeded(access);return}
        // With a Google route, hills come from the map along it (Google's elevation): the phone's
        // tilt is not used, so there is nothing to calibrate and no level ground to find. Without
        // a destination there is no route to read heights along; hills can then only come from
        // the phone's tilt, which needs to be shown what level is: with that already learned,
        // Start uses it; otherwise Start drives without hills, and "Hills too" learns it first.
        val routed=useGoogle&&googleNavigation?.state?.let {it.routeReady||it.guiding}==true
        val mapHills=routed&&!hillsUnavailable
        val levelKnown=!mapHills&&setup.levelAllowed(core.calibrationId())
        startWithMapHills=mapHills;startButtonLabel="Start"
        // With more than one learner, whose record this drive adds to.
        val who=if(learners.count>1)"Driving: ${learners.current.name}. Someone else? Tap the name at the top of Get ready first.\n\n" else ""
        val checklist="${who}Before you start:\n•  The phone is firmly mounted, rear camera facing the road.\n•  A licensed driver is beside you.\n"+
            "•  Keep watching the road. The coach can miss things, and silence never means the road is clear."
        val hills=when {
            mapHills->"Hills come from the map along your route. ${mapHillsNow()}"
            levelKnown->"Hills come from the phone's tilt, from the level ground it has learned."
            else->"${if(routed)"$hillsDetail. " else "There is no route to read hills from. "}For hills too, park on level ground and tap Hills too, then hold still for about 5 seconds while the phone learns what level is."
        }
        val builder=android.app.AlertDialog.Builder(this).setTitle("Ready to drive?").setNegativeButton("Back",null)
            .setMessage("$checklist\n\n$hills\n\n${startReadinessMessage()}").setPositiveButton(startButtonLabel,null)
        if(!mapHills&&!levelKnown)builder.setNeutralButton("Hills too",null)
        val confirmation=builder.create()
        sessionStartDialog=confirmation
        confirmation.setOnDismissListener {
            if(sessionStartDialog===confirmation)sessionStartDialog=null
            if(levelStart.isPending){levelStart.cancel();setup.skipLevelReference();publishOptions()}
        }
        confirmation.setOnShowListener {
            val neutral=confirmation.getButton(android.app.AlertDialog.BUTTON_NEUTRAL)
            // Start with no level reference: hills from the map, or no hill coaching.
            val startWithoutReference={
                levelStart.cancel();setup.skipLevelReference();publishOptions()
                if(!resumed||!roadSource()||setup.isMonitoring()||knownMoving()) {
                    setStartDialogMessage("Not started yet. ${startReadinessMessage()}")
                } else {
                    check(levelStart.request(now(),generation.get().toLong(),false))
                    confirmation.getButton(android.app.AlertDialog.BUTTON_POSITIVE).apply {text="Starting…";isEnabled=false}
                    neutral?.isEnabled=false
                    setStartDialogMessage(if(mapHills)"Starting. Waiting for a fresh look at the road. Stay parked; Back cancels this start."
                        else "Starting without hill coaching. Waiting for a fresh look at the road. Stay parked; Back cancels this start.")
                }
            }
            // Start with hills from the phone's tilt: the level reference it has, or one learned now.
            val startWithReference={
                val current=lastCompletedVision?.takeIf {it.frame?.sourceId==generation.get().toLong()&&now()-it.time in 0.0..<.35}
                if(!resumed||!roadSource()||setup.isMonitoring()||knownMoving()||CameraVisionHealth.paused(cameraThermal)||cameraError!=null||visionError!=null||current?.spatialReadiness==SpatialReadiness.MOUNT_UNSUPPORTED) {
                    setStartDialogMessage("Not started yet. ${startReadinessMessage()} Stay parked, then try again.")
                } else if(!sensors.motionAvailable) {
                    setStartDialogMessage("Hills need the phone's motion sensor, which is not available: ${sensors.motionStatus}. Tap Start to drive without hills.")
                } else {
                    val needsReference=!setup.levelAllowed(core.calibrationId())
                    if(needsReference)resetPhoneReference(true)
                    check(levelStart.request(now(),generation.get().toLong()))
                    confirmation.getButton(android.app.AlertDialog.BUTTON_POSITIVE).apply {text=if(needsReference)"Learning level…" else "Starting…";isEnabled=false}
                    neutral?.isEnabled=false
                    setStartDialogMessage(if(needsReference)"Hold still on the level ground for about 5 seconds while the phone learns what level is. Coaching starts after that and a fresh look at the road. Back cancels this start."
                        else "Level ground is known. Waiting for a fresh look at the road. Stay parked; Back cancels this start.")
                }
            }
            confirmation.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                if(levelStart.isPending)return@setOnClickListener
                if(levelKnown)startWithReference() else startWithoutReference()
                updateSetupUi()
            }
            neutral?.setOnClickListener {if(!levelStart.isPending){startWithReference();updateSetupUi()}}
        }
        confirmation.show()
    }
    /** What the map says about hills on the chosen route so far, as a sentence for the start dialog. */
    private fun mapHillsNow():String=when(hillsState) {
        RouteElevation.State.READY->hillsDetail.substringAfter(" · ",hillsDetail).replaceFirstChar {it.titlecase()}+"."
        RouteElevation.State.FETCHING->"Its heights are loading now."
        else->if(hillsDetail==RouteElevation.CHOOSE_TEXT)"Its heights load once the route is ready." else "$hillsDetail."
    }
    private fun startReadinessMessage():String=when {
        knownMoving()->"Park safely before starting."
        CameraVisionHealth.paused(cameraThermal)->"The phone is too warm for camera coaching. Let it cool."
        cameraError!=null->"Camera unavailable: $cameraError"
        visionError!=null->"Road detection stopped. Close this app fully and open it again."
        !cameraRunning->"Waiting for the rear camera."
        setup.referencePending()->"Hold still while the level reference is calibrated."
        lastCompletedVision?.frame?.sourceId!=generation.get().toLong()->"Waiting for a current camera result."
        lastCompletedVision?.spatialReadiness==SpatialReadiness.MOUNT_UNSUPPORTED->"Adjust the mount so the rear camera faces forward towards the road."
        lastCompletedVision?.let {now()-it.time !in 0.0..<.35}!=false->"Waiting for fresh camera evidence."
        else->"The camera sees the road."
    }
    private fun setStartDialogMessage(message:String) {
        sessionStartDialog?.findViewById<TextView>(android.R.id.message)?.setUiText(message)
    }
    private fun commitRoadSession():Boolean {
        if(!canStartRoad())return false
        val t=now();val up=core.cameraUp(t)
        if(!setup.start(t,up[0],up[1],up[2]))return false
        levelStart.cancel();practiceReview.start(t);lessonBaseline=org.json.JSONObject(core.driveContext());sessionStartDialog?.dismiss()
        core.learnerNewDrive()
        // The drive's focus: chosen and briefed once the route has been read, while still parked.
        focusSession=null;lastFocus=null;focusBrief=FocusBrief(t);focusRouteAt=Double.NEGATIVE_INFINITY
        focusCard=null;googleTurnsAhead=null
        // The starting checks and moving off (StartRoutine), said parked around today's focus.
        opening=if(learnerSettings().getBoolean("startChecks",true))
            Opening(StartRoutine.steps(voiceLanguage,manualCar,coachingSettings.profile)).also {o->if(coachingSettings.voiceEnabled)o.steps.forEach {lineClip(it.say)}}
            else null
        audio.cancel();activeMap.setFollowing(true)
        if(useGoogle)googleNavigation?.startGuidance()
        startCamera();publishOptions();updateSetupUi();return true
    }
    private fun updateAutomaticLevelStart() {
        if(!levelStart.isPending)return
        val result=levelStart.poll(now(),generation.get().toLong(),resumed&&roadSource(),knownMoving(),
            (!levelStart.requiresReference()||sensors.motionAvailable)&&cameraError==null&&visionError==null&&!CameraVisionHealth.paused(cameraThermal),
            setup.referencePending(),setup.levelAllowed(core.calibrationId()),canStartRoad())
        when(result) {
            LevelReferenceStart.READY->if(!commitRoadSession()) {
                sessionStartDialog?.getButton(android.app.AlertDialog.BUTTON_POSITIVE)?.apply {text=startButtonLabel;isEnabled=true}
                sessionStartDialog?.getButton(android.app.AlertDialog.BUTTON_NEUTRAL)?.isEnabled=true
                setStartDialogMessage("Not started yet. ${startReadinessMessage()} Try again while parked.")
            }
            LevelReferenceStart.CANCELLED->{
                setup.skipLevelReference();publishOptions()
                sessionStartDialog?.getButton(android.app.AlertDialog.BUTTON_POSITIVE)?.apply {text=startButtonLabel;isEnabled=true}
                sessionStartDialog?.getButton(android.app.AlertDialog.BUTTON_NEUTRAL)?.isEnabled=true
                setStartDialogMessage(if(startWithMapHills)"Start cancelled: the camera was not ready in time, or something changed. ${startReadinessMessage()} Try again when ready; coaching has not started."
                    else "Start cancelled: the phone did not learn level ground in time, or something changed. ${startReadinessMessage()} Try again when ready; coaching has not started.")
            }
        }
    }
    private fun finishPractice(interrupted:Boolean):PracticeSessionReview.Review? {
        val review=practiceReview.finish(now())
        if(review!=null&&::learningRecords.isInitialized)learningRecords.record(true,review.durationSeconds,interrupted,lessonBaseline,org.json.JSONObject(core.driveContext()))
        if(review!=null){lastLearnerSummary=core.learnerSummary();saveLearnerRecord()}
        // The focus, settled with this drive's "You first" outcomes and kept to choose the next one by
        // (a drive too short to have practised anything is not kept).
        val session=focusSession;focusSession=null;focusBrief=null;focusCard=null;opening=null
        if(::audio.isInitialized)audio.finishBrief()
        lastFocus=if(review!=null&&session!=null)session.finish(lastLearnerSummary).also {d->
            if(::learningRecords.isInitialized&&(d.measured>0||review.durationSeconds>=120))learningRecords.recordFocus(d.past(System.currentTimeMillis()))
        } else null
        return review
    }
    /** The start of a drive, every tick while coaching: the starting checks, today's focus, moving off;
     * then measuring the focus. */
    private fun driveFocusTick(t:Double,measured:Telemetry) {
        openingTick(t)
        // Today's focus follows the starting checks and the pause after the last one (at once without them).
        val o=opening
        focusBrief?.let {if(!it.done)advanceBrief(it,t,o==null||o.done||(o.pastChecks&&t>=o.nextAt&&!audio.briefPlaying))}
        // Moving off ends the brief at its next pause: driving needs the learner's attention.
        if(audio.briefPlaying&&knownMoving())audio.finishBrief()
        val session=focusSession?:return
        val speedValid=measured.speedValid&&measured.speed.isFinite()
        if(!useGoogle){session.sample(t,null,speedValid,measured.speed);return}
        // The whole route now and then (a reroute replaces it); where the car is along it, every tick.
        if(t-focusRouteAt>=1){focusRouteAt=t;DriveFocus.Route.parse(core.routeFeatures(t,true),true)?.let(session::route)}
        session.sample(t,DriveFocus.Route.parse(core.routeFeatures(t,false),false),speedValid,measured.speed)
    }
    /** Waits, parked and for up to 6 s, for the route to be read (its line, Google's turns, its
     * heights); then chooses the focus and renders it. Once [mayTalk] (the starting checks are said),
     * shows it and says it, while still parked. */
    private fun advanceBrief(b:FocusBrief,t:Double,mayTalk:Boolean) {
        val text=b.text?:run {
            val routed=useGoogle&&googleNavigation?.state?.let {it.routeReady||it.guiding}==true
            val route=if(routed)DriveFocus.Route.parse(core.routeFeatures(t,true),true) else null
            val turns=if(routed)googleTurnsAhead else null
            val heightsRead=when(hillsState) {
                RouteElevation.State.READY,RouteElevation.State.DENIED,RouteElevation.State.UNAVAILABLE->true
                RouteElevation.State.NO_ROUTE->hillsDetail!=RouteElevation.CHOOSE_TEXT // a line too short to read
                RouteElevation.State.FETCHING->false
            }
            if(routed&&!(route!=null&&route.turnsKnown&&turns!=null&&heightsRead)&&t-b.startedAt<6)return
            val offer=DriveFocus.offer(route,turns,routed)
            // "All": every skill, as the coach did before drives had one focus; otherwise one, chosen.
            val choice=if(learnerSettings().getString("driveFocus",null)==ALL_FOCUS)DriveFocus.Choice.everything()
                else DriveFocus.choose(offer,if(::learningRecords.isInitialized)learningRecords.focusHistory() else emptyList(),wantedFocus())
            focusSession=DriveFocus.Session(choice,offer).also {s->route?.let(s::route)}
            focusRouteAt=t;b.chosenAt=t
            focusCard=DriveFocus.card(choice,offer)
            Log.i("LearnDriveFocus","Focus ${if(choice.all)"all" else choice.topic.key} (${choice.why})")
            DriveFocus.brief(choice,offer,voiceLanguage).also {b.text=it
                // Rendered while the starting checks are said, so it is ready when its turn comes.
                if(coachingSettings.voiceEnabled&&phoneVoice.canSpeak)phoneVoice.clipOnce(it)}
        }
        if(!mayTalk)return
        if(b.sayFrom.isNaN()){b.sayFrom=t;focusCardUntil=t+25}
        // Said once, parked: on the move, or not ready in time, it stays on the screen only (and moving
        // off waits long enough for it to be read).
        if(!coachingSettings.voiceEnabled||!phoneVoice.canSpeak||knownMoving()||t-b.sayFrom>12) {
            b.done=true;phoneVoice.forget(text)
            focusCardUntil=minOf(focusCardUntil,t+8);opening?.let {it.nextAt=maxOf(it.nextAt,t+8)}
            return
        }
        if(audio.briefPlaying)return
        val pcm=phoneVoice.clipOnce(text)?:return
        if(audio.submitBrief(pcm)) {
            val seconds=pcm.bytes.size/(2.0*pcm.sampleRate)
            b.done=true;phoneVoice.forget(text);focusCardUntil=maxOf(focusCardUntil,t+seconds+2)
            opening?.let {it.nextAt=maxOf(it.nextAt,t+seconds+1)}
        }
    }
    /** The starting checks and moving off, one step at a time, parked: each said (or, with no voice,
     * shown) and given time to do; moving off waits for today's focus. Moving ends it. */
    private fun openingTick(t:Double) {
        val o=opening?:return
        if(o.done)return
        // Moving: the learner is driving now. What is being said ends at its next pause.
        if(knownMoving()){if(audio.briefPlaying)audio.finishBrief();o.done=true;o.shownUntil=minOf(o.shownUntil,t+3);return}
        if(audio.briefPlaying||t<o.nextAt)return
        val step=o.steps.getOrNull(o.index)?:run {o.done=true;return}
        if(step.part==StartRoutine.Part.MOVE_OFF&&focusBrief?.done==false)return
        o.shown=o.index
        val talk=coachingSettings.voiceEnabled&&::phoneVoice.isInitialized&&phoneVoice.canSpeak
        val pcm=if(talk)lineClip(step.say) else null
        if(pcm==null) {
            // Still being rendered: a moment's wait; then (or with no voice) on the screen only.
            if(talk) {
                if(o.waitingSince.isNaN())o.waitingSince=t
                if(t-o.waitingSince<3){o.shownUntil=maxOf(o.shownUntil,t+1);return}
            }
            o.waitingSince=Double.NaN;o.index++;o.nextAt=t+4+step.pause;o.shownUntil=o.nextAt;return
        }
        // Anything the coach has to say goes first; this step waits for it.
        if(!audio.submitBrief(pcm))return
        o.waitingSince=Double.NaN;o.index++
        o.nextAt=t+pcm.bytes.size/(2.0*pcm.sampleRate)+step.pause;o.shownUntil=o.nextAt
    }
    /** A starting line in the coach's voice: a recorded take of these words when the recorded voice is
     * chosen and has one, else the phone's voice (null while it renders). */
    private fun lineClip(text:String):ai.learndrive.mobile.audio.Pcm? {
        if(voiceKind==RECORDED_VOICE)appVoice?.takeIf {it.complete}?.clip(text)?.let {return it}
        return if(::phoneVoice.isInitialized)phoneVoice.clip(text) else null
    }
    /** The starting step on the coach card while it is said and done (instructions still take the card). */
    private fun openingCard(t:Double):ai.learndrive.mobile.ui.CoachCard? {
        val o=opening?:return null
        val step=o.steps.getOrNull(o.shown)?:return null
        if(t>=o.shownUntil)return null
        val part=o.steps.filter {it.part==step.part}
        return DriveCards.routine(if(step.part==StartRoutine.Part.CHECKS)"BEFORE YOU START" else "MOVING OFF",step.title,step.text,
            part.map {it.title},part.indexOf(step))
    }
    private fun endRoadSession() {
        val review=finishPractice(false)
        if(review!=null)lastPracticeReview=review
        setup.stop();audio.cancel();coachCaption.clear();googleNavigation?.stopGuidance();showSetup=true
        if(resumed&&mode=="Camera")startCamera()
        // The next drive may be someone else's.
        whoAsked=false
        publishOptions();updateSetupUi()
        // The review opens by itself, parked (asked to confirm when the speed is unknown).
        if(review!=null)openSessionReview()
    }
    private fun showSessionReview() {
        if(PracticeSessionReview.access(setup.isMonitoring(),knownMoving(),lastPracticeReview!=null,true)!=PracticeSessionReview.Access.READY)return
        val review=lastPracticeReview?:return
        val whole=review.text()+"\n\n"+LearnerReview.drive(lastLearnerSummary)
        val focus=lastFocus
        if(focus==null) {
            presentSessionReviewDialog(android.app.AlertDialog.Builder(this).setTitle("Drive review").setMessage(whole).setPositiveButton("Done",null).create())
            return
        }
        // The drive's focus first (one skill, or every skill for "All"); the whole drive's cues one tap away.
        val extra=if(DriveFocus.Topic.ROAD_USERS in focus.topics)LearnerReview.status(lastLearnerSummary) else null
        presentSessionReviewDialog(android.app.AlertDialog.Builder(this).setTitle("Today's focus: ${focus.title}")
            .setMessage(DriveFocus.review(focus,extra)+"\n\n"+review.summary()).setPositiveButton("Done",null)
            .setNeutralButton("Whole drive") {_,_->presentSessionReviewDialog(android.app.AlertDialog.Builder(this)
                .setTitle("Whole drive").setMessage(whole).setPositiveButton("Done",null).create())}.create())
    }
    private fun saveLearnerRecord() {
        if(!::learnerRecord.isInitialized)return
        // The store of the learner driving now, taken here: a save still queued when another learner
        // takes over writes this learner's file, never theirs.
        val record=learnerRecord
        evidenceIo.execute {runCatching {record.save()}}
    }
    /** The coach settings of [id] (by default the learner driving): level, voice, language, "You first". */
    private fun learnerSettings(id:String=learners.current.id)=getSharedPreferences(learners.storage(id).settings,MODE_PRIVATE)
    private fun learnerRecords(id:String=learners.current.id)=getSharedPreferences(learners.storage(id).records,MODE_PRIVATE)
    private fun learnerFile(id:String=learners.current.id)=java.io.File(filesDir,learners.storage(id).recordPath)
    /** Setup's drive focus choices: the key kept in the learner's settings, and what they read. */
    private val FOCUS_CHOICES=listOf("auto" to "Coach picks",ALL_FOCUS to "All")+DriveFocus.Topic.entries.map {it.key to it.noun.replaceFirstChar {c->c.titlecase()}}
    /** The coach's voices, as Get ready offers them: language and kind, and what a learner reads. */
    private val VOICE_CHOICES=listOf(Triple(ai.learndrive.mobile.audio.VoicePhrases.ENGLISH,RECORDED_VOICE,"English · recorded"),
        Triple(ai.learndrive.mobile.audio.VoicePhrases.ENGLISH,PHONE_VOICE,"English · phone voice"),
        Triple(ai.learndrive.mobile.audio.VoicePhrases.MALAYALAM,RECORDED_VOICE,"മലയാളം · recorded"),
        Triple(ai.learndrive.mobile.audio.VoicePhrases.MALAYALAM,PHONE_VOICE,"മലയാളം · phone voice"))
    private fun voiceChoiceIndex()=VOICE_CHOICES.indexOfFirst {it.first==voiceLanguage&&it.second==voiceKind}.coerceAtLeast(0)
    private fun storedVoiceKind(settings:android.content.SharedPreferences)=settings.getString("voiceKind",null)?.takeIf {it==PHONE_VOICE}?:RECORDED_VOICE
    /** A voice from Get ready or first open: its language (the voices start again) and kind (the order they speak in). Parked only. */
    private fun setVoice(index:Int) {
        val (language,kind,_)=VOICE_CHOICES.getOrNull(index)?:return
        if(setup.isMonitoring()){updateSetupUi();return}
        if(kind!=voiceKind){voiceKind=kind;learnerSettings().edit().putString("voiceKind",kind).apply();applyVoice()}
        if(language!=voiceLanguage)setVoiceLanguage(language) else updateSetupUi()
    }
    private fun focusChoiceIndex(settings:android.content.SharedPreferences)=
        FOCUS_CHOICES.indexOfFirst {it.first==settings.getString("driveFocus","auto")}.coerceAtLeast(0)
    /** The learner's own choice of focus, or null when the coach chooses. */
    private fun wantedFocus()=DriveFocus.Topic.of(learnerSettings().getString("driveFocus",null))
    private fun learnerChangeAllowed()=resumed&&roadSource()&&!setup.isMonitoring()&&!knownMoving()&&!learnerSwitching
    /** Who is driving: another learner, a new one, or renaming or removing the one driving. Parked only. */
    private fun chooseLearner() {
        if(!learnerChangeAllowed())return
        whoAsked=true
        val list=learners.all;val current=learners.current
        val items=list.map {if(it.id==current.id)"${it.name} (driving now)" else it.name}.toMutableList()
        val add=if(list.size<Learners.MAX_LEARNERS){items+="Add a learner…";items.size-1} else -1
        items+="Rename ${current.name}…";val rename=items.size-1
        val remove=if(list.size>1){items+="Remove ${current.name}…";items.size-1} else -1
        learnerDialog?.dismiss()
        learnerDialog=android.app.AlertDialog.Builder(this).setTitle("Who's driving?")
            .setItems(items.toTypedArray()) {_,which->when(which) {
                in list.indices->switchLearner(list[which].id)
                add->askLearnerName("Add a learner","") {name->learners.add(name)?.also {switchLearner(it.id)}!=null}
                rename->askLearnerName("Rename ${current.name}",current.name) {name->learners.rename(current.id,name).also {if(it)updateSetupUi()}}
                remove->confirmRemoveLearner(current)
            }}
            .setNegativeButton("Close",null).show()
    }
    /** Asks for a learner's name; [accept] says whether it was taken (1-30 characters, no other learner's). */
    private fun askLearnerName(title:String,initial:String,accept:(String)->Boolean) {
        if(!learnerChangeAllowed())return
        val input=android.widget.EditText(this).apply {
            setText(initial);setSelection(initial.length);isSingleLine=true;hint="Name"
            filters=arrayOf(android.text.InputFilter.LengthFilter(Learners.MAX_NAME))
        }
        val pad=CoachStyle.dp(this,20)
        val box=FrameLayout(this).apply {setPadding(pad,CoachStyle.dp(this@MainActivity,8),pad,0);addView(input)}
        val dialog=android.app.AlertDialog.Builder(this).setTitle(title).setView(box)
            .setPositiveButton("Save",null).setNegativeButton("Cancel",null).create()
        dialog.setOnShowListener {
            dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                if(!learnerChangeAllowed())dialog.dismiss()
                else if(accept(input.text.toString()))dialog.dismiss()
                else input.error="Use 1-${Learners.MAX_NAME} characters and a name no other learner has"
            }
            input.requestFocus()
        }
        dialog.window?.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE)
        learnerDialog=dialog;dialog.show()
    }
    private fun confirmRemoveLearner(learner:Learners.Learner) {
        if(!learnerChangeAllowed())return
        learnerDialog=android.app.AlertDialog.Builder(this).setTitle("Remove ${learner.name}?")
            .setMessage("Their practice records, \"You first\" record and coach settings are deleted from this phone. This cannot be undone.")
            .setNegativeButton("Cancel",null).setPositiveButton("Remove") {_,_->removeLearner(learner.id)}.show()
    }
    private fun removeLearner(id:String) {
        if(!learnerChangeAllowed()||learners.count<2)return
        if(learners.current.id==id)switchLearner(learners.all.first {it.id!=id}.id)
        if(learners.current.id==id)return
        val where=learners.remove(id)?:return
        // Queued after the switch's save of their record: everything of theirs goes, then.
        evidenceIo.execute {runCatching {deleteLearnerData(where)}}
        updateSetupUi()
    }
    /** Deletes one learner's data from this phone. On evidenceIo, after anything queued for them. */
    private fun deleteLearnerData(where:Learners.Storage) {
        if(where.legacy) {
            // The phone's first learner shares these files with the phone's and the car's own settings: only their keys go.
            getSharedPreferences(where.settings,MODE_PRIVATE).edit().apply {Learners.LEGACY_SETTINGS.forEach(::remove)}.commit()
            getSharedPreferences(where.records,MODE_PRIVATE).edit().remove("sessions").remove(LearningRecordStore.FOCUS).commit()
            java.io.File(filesDir,where.recordPath).delete()
        } else {
            getSharedPreferences(where.settings,MODE_PRIVATE).edit().clear().commit()
            deleteSharedPreferences(where.settings)
            val dir=java.io.File(filesDir,where.recordPath).parentFile
            if(dir?.parentFile?.name=="learners"&&dir.parentFile?.parentFile==filesDir)dir.deleteRecursively()
        }
    }
    /** Another learner drives from now: their record, practice records and coach settings. Parked only. */
    private fun switchLearner(id:String) {
        if(id==learners.current.id||!learnerChangeAllowed()||!learners.select(id))return
        val old=learnerRecord;val fresh=LearnerRecordStore(learnerFile(id),core);learnerRecord=fresh
        // One queue, in order: the last learner's record is saved before the coach is given the next one's.
        learnerSwitching=true
        evidenceIo.execute {
            runCatching {old.save()};runCatching {core.learnerForget()};runCatching {fresh.load()}
            main.post {learnerSwitching=false;updateSetupUi()}
        }
        val settings=learnerSettings(id)
        coachingPreferences=CoachingPreferences(settings)
        val stored=coachingPreferences.load()
        applyCoachingSettings(stored.profile,stored.voiceEnabled,stored.revision)
        core.learnerWaiting(settings.getBoolean("youFirst",true))
        learningRecords=LearningRecordStore(learnerRecords(id))
        lastPracticeReview=null;lastLearnerSummary=null;lastFocus=null
        val language=storedVoiceLanguage(settings)
        val kind=storedVoiceKind(settings)
        if(kind!=voiceKind){voiceKind=kind;applyVoice()}
        // Another language, or another phone voice chosen for it: the voices start again.
        if(language!=voiceLanguage||preferredPhoneVoice(id,language)!=phoneVoiceChosen){voiceLanguage=language;restartPhoneVoice()}
        // Get ready shows the new learner's choices from their state (updateSetupUi); an open page is built again for them.
        pageKind?.let(::openPage)
        updateSetupUi()
    }
    /** May the learner driving start now? Asked only parked, before a drive (see DriveAccess). When
     * billing comes, a confirmed check also sets latestSeen to the server's time, so a phone clock
     * once set far ahead cannot lock a learner out. */
    private fun driveAccess():DriveAccess.Decision {
        val wall=System.currentTimeMillis()
        val phone=getSharedPreferences("drive_access",MODE_PRIVATE)
        val seen=maxOf(wall,phone.getLong("latestSeen",0L))
        phone.edit().putLong("latestSeen",seen).apply()
        val s=learnerSettings()
        val paid=if(s.contains("paidUntil"))DriveAccess.Paid(s.getLong("paidUntil",0L),s.getLong("paidCheckedAt",0L)) else null
        return DriveAccess.decide(BuildConfig.SUBSCRIPTION_REQUIRED,wall,seen,paid)
    }
    private fun showSubscriptionNeeded(decision:DriveAccess.Decision) {
        val name=learners.current.name
        android.app.AlertDialog.Builder(this).setTitle("Subscription needed")
            .setMessage(if(decision==DriveAccess.Decision.LAPSED)"$name's subscription has ended. Renew it to keep practising."
                else "$name needs a subscription to start practising.")
            .setPositiveButton("OK",null).show()
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
        if(monitoring||moving){sessionReviewDialog?.dismiss();learnerDialog?.dismiss();closePage()}
        dashboard.keepScreenOn=monitoring
        dashboard.setSessionRunning(monitoring)
        dashboard.sessionStatus.setUiText(if(monitoring)"Supervised" else if(roadSource())"Parked" else "Test input")
        val editable=resumed&&roadSource()&&!monitoring&&!moving
        dashboard.learnerChip.setUiText(learners.current.name)
        dashboard.learnerChip.isEnabled=editable&&!learnerSwitching
        destinationPanel.setRunning(resumed&&roadSource()&&!useGoogle)
        destinationPanel.setEditable(editable&&!useGoogle)
        googleNavigation?.setEditable(editable&&useGoogle)
        googleProviderButton.isEnabled=editable&&googleNavigation!=null
        offlineProviderButton.isEnabled=editable
        rightPane.visibility=if(monitoring)View.GONE else View.VISIBLE
        dashboard.showSetup(!monitoring&&showSetup)
        dashboard.showDestinationPage(showDestination)
        dashboard.settingsButton.isEnabled=!monitoring&&!moving
        dashboard.settingsButton.setUiText(if(monitoring)"" else if(showSetup)"Close setup" else "Setup")
        dashboard.setSetupCompact(monitoring)
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
        labToggle.visibility=if(developerTools||mode!="Camera")View.VISIBLE else View.GONE
        if(mode=="Camera"&&moving){labPanel.visibility=View.GONE;labToggle.isEnabled=false}else labToggle.isEnabled=true
        labToggle.setUiText(if(showLab)"Hide developer tools" else "Developer tools")
        refreshBefore(editable)
        // First open's questions wait while the car moves. With two or more learners, "Who's
        // driving?" comes first before a drive.
        onboarding?.let {it.visibility=if(monitoring||moving)View.GONE else View.VISIBLE;it.refresh()}
        if(showSetup&&editable&&onboarding==null&&page==null&&!learnerSwitching&&learnerDialog?.isShowing!=true&&
            ParkedFlow.askWhoIsDriving(learners.count,whoAsked)){whoAsked=true;main.post {chooseLearner()}}
        if(mode!="Camera")return
        val frame=lastCompletedVision?.takeIf {it.frame?.sourceId==generation.get().toLong()}
        val (cameraLine,cameraOk)=cameraCheck()
        dashboard.setReadiness(0,cameraLine,cameraOk)
        val voiceLine=when {
            !coachingSettings.voiceEnabled->"Voice off · instructions on screen only"
            !audio.ready->"$audioStatus · instructions on screen meanwhile"
            // The recorded voice while it unpacks (the first start of a version), or what went wrong.
            recordedVoiceSpeaks()&&!appVoice!!.complete->appVoiceStatus.ifBlank {"Coach voice: preparing"}
            recordedVoiceSpeaks()&&appVoice!!.missing>0->"Ready · ${appVoice!!.missing} phrases in the phone voice"
            // The phone's voice until it is rendered: its progress, or what is missing (English recordings speak meanwhile).
            !recordedVoiceSpeaks()&&!phoneVoice.complete->voiceStatus
            voiceLanguage!=ai.learndrive.mobile.audio.VoicePhrases.ENGLISH&&!phoneVoice.complete->voiceStatus
            else->"Ready · tap to hear it"
        }
        dashboard.setReadiness(1,voiceLine,!coachingSettings.voiceEnabled||audio.ready)
        val measured=core.snapshot(now())
        dashboard.setReadiness(2,if(measured.speedValid)"Measured speed available" else if(permissionsGranted(true))"No reliable speed yet" else "Tap to allow location",measured.speedValid)
        dashboard.setReadiness(3,when {
            !useGoogle->"Offline region: the phone's tilt is used"
            // A route is chosen but its line has not reached the elevation reader yet.
            hillsState==RouteElevation.State.NO_ROUTE&&hillsDetail==RouteElevation.CHOOSE_TEXT&&
                googleNavigation?.state?.let {it.routeReady||it.guiding}==true->"Route chosen · reading its hills next"
            else->hillsDetail
        },useGoogle&&hillsReady)
        val alignment=frame?.let {SpatialReadiness.status(it.spatialReadiness)}?:"Waiting for camera analysis"
        val heading=if(setup.state()==DriveSetup.CHECK_MOUNT)"Check mount: roll changed while measured speed was near zero. Recheck while parked."
            else if(!roadSource())"Lab screen test selected. Select the road camera to set up a mounted session."
            else if(moving)"Setup paused: measured speed is above 1 km/h. Stop and park before setup."
            else "Confirm the fixed forward-facing mount before starting."
        setupStatus.setUiText("$heading\n$alignment\nCoaching: ${coachingSettings.profile.label} · voice ${if(coachingSettings.voiceEnabled)"$audioStatus · $voiceStatus" else "off"}\n${if(useGoogle&&!hillsUnavailable)"Hills: from the map along the Google route; no level reference needed" else setup.referenceStatus()}\nMount checks need fresh measured speed and cannot detect every movement.")
        // The entry action stays stable while frame age/capability changes. Its modal
        // explains readiness; both actual admission paths still enforce canStartRoad.
        setupStart.setUiText(if(!roadSource())"Use mounted road camera" else "Start drive")
        setupStart.isEnabled=if(!roadSource())true else resumed&&!setup.isMonitoring()&&!moving
    }
    /** What the camera sees now, in a learner's words, and whether that is ready for a drive. */
    private fun cameraCheck():Pair<String,Boolean> {
        val frame=lastCompletedVision?.takeIf {it.frame?.sourceId==generation.get().toLong()}
        val fresh=frame!=null&&now()-frame.time in 0.0..<.35&&cameraRunning&&cameraError==null&&visionError==null&&!CameraVisionHealth.paused(cameraThermal)
        val line=when {
            !permissionsGranted(false)->"Allow the camera, so it can see the road"
            cameraError!=null->cameraError!!
            visionError!=null->"Road detection stopped · reopen the app"
            !cameraRunning->"Starting the rear camera…"
            frame?.spatialReadiness==SpatialReadiness.MOUNT_UNSUPPORTED->"Point the rear camera towards the road"
            !fresh->"Waiting for the camera"
            frame?.spatialReadiness==SpatialReadiness.READY->"Sees the road ahead"
            else->"Sees the road · distances unavailable"
        }
        return line to (fresh&&frame?.spatialReadiness!=SpatialReadiness.MOUNT_UNSUPPORTED)
    }
    private fun permissionsGranted(includeLocation:Boolean)=
        (listOf(Manifest.permission.CAMERA)+if(includeLocation)listOf(Manifest.permission.ACCESS_FINE_LOCATION) else emptyList())
            .all {ContextCompat.checkSelfPermission(this,it)==PackageManager.PERMISSION_GRANTED}
    /** Get ready shows the learner's choices as they are now, and takes changes only parked. */
    private fun refreshBefore(editable:Boolean) {
        if(!::before.isInitialized)return
        val level=coachingSettings.profile
        before.level.select(ParkedFlow.LEVELS.indexOf(level));before.levelLine.setUiText(ParkedFlow.levelLine(level))
        before.voice.select(voiceChoiceIndex())
        val note=voiceNote();before.voiceNote.setUiText(note.orEmpty());before.voiceNote.visibility=if(note==null)View.GONE else View.VISIBLE
        before.getVoice.visibility=if(canGetVoice())View.VISIBLE else View.GONE
        val focus=focusChoiceIndex(learnerSettings());before.focus.select(focus);before.focusLine.setUiText(focusLine(focus))
        for(choice in listOf(before.level,before.voice,before.focus))if(choice.isEnabled!=editable)choice.isEnabled=editable
        before.hear.isEnabled=!setup.isMonitoring()
    }
    /** What today's focus choice means, in one line. */
    private fun focusLine(index:Int):String {
        val routed=useGoogle&&googleNavigation?.state?.let {it.routeReady||it.guiding}==true
        val key=FOCUS_CHOICES.getOrNull(index)?.first
        val topic=DriveFocus.Topic.of(key)
        return when {
            key==ALL_FOCUS->"The coach covers everything, and the review after the drive goes through every hill, bend, turn and road user."+
                if(routed)"" else " Without a destination, only road users can be measured."
            topic==null->"The coach picks one from your route and your last drives. The review after the drive is about it."
            topic==DriveFocus.Topic.ROAD_USERS->"Every drive focuses on road users. The review after the drive is about them."
            !routed->"${topic.noun.replaceFirstChar {it.titlecase()}} need a Google destination. Without one, the focus is road users."
            else->"Every drive focuses on ${topic.noun} when the route has them; otherwise the coach picks."
        }
    }
    /** The recorded voice is chosen and the app has one in this language. */
    private fun recordedVoiceSpeaks()=voiceKind==RECORDED_VOICE&&appVoice!=null
    /** The line under the coach's voice: which voice it is, or what to know about it. */
    private fun voiceNote():String?=when {
        !coachingSettings.voiceEnabled->"The coach's voice is off (Settings): instructions show on screen only."
        voiceKind==RECORDED_VOICE&&appVoice==null->"There is no recording in this language yet: the phone voice speaks."
        ::phoneVoice.isInitialized&&!phoneVoice.complete&&(!recordedVoiceSpeaks()||voiceLanguage!=ai.learndrive.mobile.audio.VoicePhrases.ENGLISH)->voiceStatus
        voiceKind==RECORDED_VOICE->"A real person's recorded voice. New sentences, such as the starting checks, are in the phone voice."
        else->"The phone's own friendly voice"+(if(::phoneVoice.isInitialized)phoneVoice.description?.let {" · $it"}.orEmpty() else "")+". Another one: Settings."
    }
    private fun canGetVoice()=voiceLanguage!=ai.learndrive.mobile.audio.VoicePhrases.ENGLISH&&::phoneVoice.isInitialized&&phoneVoice.missingVoice
    /** A sample of the coach's voice as chosen: the recorded voice's own take, or a sentence in the
     * phone's voice rendered now (so a newly chosen phone voice can be heard straight away). */
    private fun hearVoice(phone:Boolean=false) {
        if(!::audio.isInitialized||setup.isMonitoring())return
        audio.cancel()
        if((phone||!recordedVoiceSpeaks())&&::phoneVoice.isInitialized&&phoneVoice.canSpeak) {
            // Not one of the coach's phrases: those stay rendered and kept as they are.
            pendingSample=if(voiceLanguage==ai.learndrive.mobile.audio.VoicePhrases.MALAYALAM)"നമസ്കാരം! ഞാൻ നിങ്ങളുടെ ഡ്രൈവിംഗ് കോച്ചാണ്. നമുക്ക് ഒരുമിച്ച് സുരക്ഷിതമായി ഓടിക്കാം."
                else "Hello! I'm your driving coach. Let's drive safely together."
            pendingSampleSince=now();sampleTick()
        }
        else if(!audio.submit(2))status.text="Voice unavailable: $audioStatus · check media volume"
    }
    /** Says the phone-voice sample once it is rendered (given up after 6 s). */
    private fun sampleTick() {
        val text=pendingSample?:return
        if(setup.isMonitoring()||now()-pendingSampleSince>6){pendingSample=null;phoneVoice.forget(text);return}
        val pcm=phoneVoice.clipOnce(text)?:return
        pendingSample=null;phoneVoice.forget(text)
        if(!audio.submitBrief(pcm))status.text="Voice unavailable: $audioStatus · check media volume"
    }
    /** A coaching level the learner chose (Get ready, first open, or the hint in Progress), from now on. */
    private fun chooseLevel(profile:CoachingProfile) {
        if(setup.isMonitoring()||profile==coachingSettings.profile){updateSetupUi();return}
        changeCoachingSettings(profile,coachingSettings.voiceEnabled)
        // The hint to move on looks only at drives at this level.
        learnerSettings().edit().putLong("levelSince",System.currentTimeMillis()).apply()
        updateSetupUi()
    }
    private fun setManualCar(on:Boolean) {
        manualCar=on;getSharedPreferences("driving_advice",MODE_PRIVATE).edit().putBoolean("manualVehicle",on).apply();publishOptions()
    }
    private fun setSlopeGuidance(on:Boolean) {
        slopeGuidance=on;getSharedPreferences("driving_advice",MODE_PRIVATE).edit().putBoolean("slopeGuidance",on).apply();publishOptions()
    }
    /** A readiness check tapped on Get ready: what can be done about it. */
    private fun readinessTapped(index:Int) {
        if(setup.isMonitoring()||knownMoving())return
        when(index) {
            // The camera: allow it, or show the road view to aim the phone.
            0->if(!permissionsGranted(false))requestPermissions() else {showSetup=false;updateSetupUi()}
            1->hearVoice()
            2->requestPermissions(includeLocation=true)
            3->if(!(useGoogle&&googleNavigation?.state?.let {it.routeReady||it.guiding}==true))openDestination()
        }
    }

    // ---------------------------------------------------------------- first open, and the pages

    /** First open's questions (ParkedFlow.Step), over the parked screen until the last one is done. */
    private fun showOnboarding() {
        if(onboarding!=null)return
        showSetup=false;showDestination=false
        val view=ai.learndrive.mobile.ui.Onboarding(this,object:ai.learndrive.mobile.ui.Onboarding.Host {
            override fun voices()=VOICE_CHOICES.map {it.third}
            override fun voice()=voiceChoiceIndex()
            override fun setVoice(index:Int)=this@MainActivity.setVoice(index)
            override fun hear()=hearVoice()
            override fun voiceNote()=this@MainActivity.voiceNote()
            override fun canGetVoice()=this@MainActivity.canGetVoice()
            override fun getVoice()=installVoiceData()
            override fun name()=learners.current.name
            override fun rename(raw:String):Boolean {
                val name=Learners.cleanName(raw)?:return false
                return name==learners.current.name||learners.rename(learners.current.id,name)
            }
            override fun manual()=manualCar
            override fun setManual(on:Boolean)=setManualCar(on)
            override fun profile()=coachingSettings.profile
            override fun setProfile(profile:CoachingProfile)=chooseLevel(profile)
            override fun camera()=cameraCheck()
            override fun needsPermission()=!permissionsGranted(true)
            override fun allow()=requestPermissions(includeLocation=true)
            override fun leftColumnWidth()=dashboard.leftColumnWidth
            override fun finished()=finishOnboarding()
        })
        onboarding=view;dashboard.overlayHost.addView(view,FrameLayout.LayoutParams(-1,-1));updateSetupUi()
    }
    private fun finishOnboarding() {
        flowPrefs.edit().putBoolean("onboarded",true).apply()
        hideKeyboard();onboarding?.let {dashboard.overlayHost.removeView(it)};onboarding=null
        // They have just said who they are.
        whoAsked=true
        if(!permissionsGranted(false))requestPermissions()
        showDestination=false;showSetup=true;updateSetupUi()
    }
    /** Progress, Settings or About over the parked screen, built afresh from the learner's data; parked only. */
    private fun openPage(kind:String) {
        if(setup.isMonitoring()||knownMoving())return
        val title=when(kind) {"progress"->"Progress";"settings"->"Settings";else->"About LearnDrive"}
        val fresh=ai.learndrive.mobile.ui.Page(this,title) {closePage()}
        when(kind) {"progress"->fillProgress(fresh);"settings"->fillSettings(fresh);else->fillAbout(fresh)}
        page?.let {dashboard.overlayHost.removeView(it)}
        page=fresh;pageKind=kind;dashboard.overlayHost.addView(fresh,FrameLayout.LayoutParams(-1,-1))
    }
    private fun closePage() {
        val open=page?:return
        dashboard.overlayHost.removeView(open);page=null;pageKind=null
    }
    private fun fillProgress(p:ai.learndrive.mobile.ui.Page) {
        val name=learners.current.name
        if(learners.count>1)p.paragraph("$name's drives on this phone.",CoachStyle.muted)
        if(lastPracticeReview!=null){p.section("Last drive");p.button("Open the last drive's review") {openSessionReview()}}
        val history=learningRecords.focusHistory()
        ParkedFlow.nextLevel(coachingSettings.profile,history,learnerSettings().getLong("levelSince",0L))?.let {less->
            p.section("Ready for less coaching?")
            p.paragraph("Your last ${ParkedFlow.DRIVES} drives at ${coachingSettings.profile.label} went well: most of what the coach measured was done as it asks. ${less.label}: ${ParkedFlow.levelLine(less)}")
            p.button("Switch to ${less.label}",true) {chooseLevel(less);openPage("progress")}
        }
        p.section("Each skill")
        val skills=ParkedFlow.skillLines(history)
        if(skills.isEmpty())p.paragraph("Nothing yet. Each drive has one focus (hills, bends, turns or road users), and how it went shows here.",CoachStyle.muted)
        else skills.forEach {p.paragraph(it)}
        p.section("You first")
        p.paragraph(LearnerReview.status(core.learnerSummary()).ifBlank {"The coach has no record yet of how you respond to road users."})
        p.section("Your drives")
        val drives=learningRecords.drives().take(10)
        if(drives.isEmpty())p.paragraph("No drives saved yet.",CoachStyle.muted)
        val format=java.text.DateFormat.getDateTimeInstance(java.text.DateFormat.MEDIUM,java.text.DateFormat.SHORT)
        for(d in drives)p.paragraph("${format.format(java.util.Date(d.at))} · ${if(d.seconds<60)"${d.seconds.toInt()} s" else "${(d.seconds/60).roundToInt()} min"}${if(d.interrupted)" · interrupted" else ""}")
        p.paragraph("The coach measures your speed and sees the road. It cannot see your mirrors, signals, pedals or gears, so these are observations, not a driving grade.",CoachStyle.muted,13f)
        p.button("Clear saved records") {
            android.app.AlertDialog.Builder(this).setTitle("Clear $name's records?")
                .setMessage("Their drives, skills and \"You first\" record are deleted from this phone. This cannot be undone.")
                .setNegativeButton("Cancel",null).setPositiveButton("Clear") {_,_->
                    learningRecords.clear();val cleared=learnerRecord;evidenceIo.execute {runCatching {cleared.forget()}};openPage("progress")
                }.show()
        }
    }
    private fun fillSettings(p:ai.learndrive.mobile.ui.Page) {
        val s=learnerSettings()
        p.section("Learners")
        p.paragraph("Driving now: ${learners.current.name}. Each learner keeps their own progress and settings.")
        p.button("Change, add or rename learners") {chooseLearner()}
        p.section("Coach")
        p.switch("Say less when I've already slowed",s.getBoolean("youFirst",true)) {on->core.learnerWaiting(on);learnerSettings().edit().putBoolean("youFirst",on).apply()}
        p.paragraph("Once your record over several drives shows that you slow for road users, the coach leaves out what your speed shows you have already done. If you have not slowed, you are told in full, and \"Brake now\" is never left out.",CoachStyle.muted,13f)
        p.switch("The coach's voice",coachingSettings.voiceEnabled) {on->changeCoachingSettings(coachingSettings.profile,on);updateSetupUi()}
        p.paragraph("Off: instructions show on screen only.",CoachStyle.muted,13f)
        p.switch("Starting checks before each drive",s.getBoolean("startChecks",true)) {on->learnerSettings().edit().putBoolean("startChecks",on).apply()}
        p.paragraph("Parked, after Start: the checks before you start the engine, then today's focus, then how to move off. How much is said follows the coaching level.",CoachStyle.muted,13f)
        p.section("Phone voice")
        val names=if(::phoneVoice.isInitialized)phoneVoice.voiceNames else emptyList()
        val current=if(::phoneVoice.isInitialized)names.indexOf(phoneVoice.voiceName) else -1
        p.paragraph(when {
            !::phoneVoice.isInitialized||phoneVoice.missingVoice->"The phone has no ${ai.learndrive.mobile.audio.VoicePhrases.name(voiceLanguage)} voice of its own yet."
            current<0->"Getting the phone's voices ready… Open Settings again in a moment."
            else->"${ai.learndrive.mobile.audio.VoicePhrases.name(voiceLanguage)}: voice ${current+1} of ${names.size}"+(phoneVoice.description?.let {" · $it"}.orEmpty())
        })
        p.paragraph("The voice the coach uses when you choose the phone voice, and for new sentences such as the starting checks. Android does not say which voices are which, so listen and keep the one you like best: a new voice takes a minute to prepare every phrase.",CoachStyle.muted,13f)
        if(names.size>1)p.button("Try the next phone voice") {
            val next=names[(current+1).mod(names.size)]
            learnerSettings().edit().putString("phoneVoice.$voiceLanguage",next).apply();restartPhoneVoice();openPage("settings")
        }
        p.button("Hear the phone voice") {hearVoice(phone=true)}
        p.paragraph("Malayalam is spoken by the phone's Google voice. If it is missing, get it here (or in Android Settings › Text-to-speech); until it is ready the English recordings speak.",CoachStyle.muted,13f)
        p.button("Get the Malayalam voice") {installVoiceData()}
        p.section("Your car")
        p.add(ai.learndrive.mobile.ui.Choice(this,listOf("Manual","Automatic")) {i->setManualCar(i==0)}.apply {select(if(manualCar)0 else 1)},CoachStyle.dp(this,52))
        p.paragraph("Manual: the coach suggests the gear that suits your speed. It never sees which gear you are in.",CoachStyle.muted,13f)
        p.switch("Hill coaching",slopeGuidance) {on->setSlopeGuidance(on)}
        p.section("Hills without a destination")
        p.paragraph("With a Google destination, hills come from the map and nothing needs setting up. Without one, they come from the phone's tilt: park on level ground and let the phone learn what level is.",CoachStyle.muted,13f)
        p.button("Learn level ground now") {normalizePhone()}
        if(setup.referencePending()||setup.referenceId()!=0L)p.button("Forget level ground") {setup.skipLevelReference();publishOptions();openPage("settings")}
        p.section("Maps")
        p.paragraph("Choose Google Maps or an offline region under Where to?. An offline region file adds maps that work without a connection.",CoachStyle.muted,13f)
        p.button("Add an offline region file") {if(!setup.isMonitoring()&&!knownMoving())importRoutePack.launch(arrayOf("application/zip","application/octet-stream","*/*"))}
        p.section("Location")
        val allowed=permissionsGranted(true)
        p.paragraph(if(allowed)"Location is allowed: it gives your speed and follows your route." else "Location gives the coach your speed and follows your route.",CoachStyle.muted,13f)
        if(!allowed)p.button("Allow location") {requestPermissions(includeLocation=true)}
    }
    private fun fillAbout(p:ai.learndrive.mobile.ui.Page) {
        p.section("What LearnDrive is")
        p.paragraph("A driving coach on your windscreen. Through the phone's rear camera it watches the road ahead, and it tells you, out loud and on screen, what to do next: slow for a bend, get ready for a turn, give a road user room. With a Google destination it also knows the turns, bends and hills on your route.")
        p.section("How a drive works")
        p.paragraph("Before you drive, choose the coaching level, the coach's voice and today's focus under Get ready. Parked, after Start, the coach names today's focus. While you drive, it speaks when you need to act. Afterwards, the review goes through today's focus.")
        p.section("What it cannot see")
        p.paragraph("It measures your speed and sees the road ahead. It cannot see your pedals, your gear lever, your mirrors or where you look, and it can miss things on the road. Silence never means the road is clear.")
        p.section("Safety")
        p.paragraph("Always practise with a licensed driver beside you. Mount the phone firmly where it does not block your view. Change settings only while parked.")
        p.section("Privacy")
        p.paragraph("The camera picture is analysed on the phone and never saved. Practice records stay on this phone, separately for each learner. Google route details are used during the drive and not kept.")
        p.section("Credits")
        p.paragraph("Turn-by-turn guidance, maps and route heights: Google Maps Platform. Junction coaching without a destination uses road data © OpenStreetMap contributors (ODbL), fetched online for a coarse area around you from the public Overpass API; your exact position is not sent.",CoachStyle.muted,13f)
        p.section("Version")
        val version=p.paragraph("LearnDrive ${BuildConfig.VERSION_NAME}").apply {tag="app-version"}
        val hint=p.paragraph("",CoachStyle.muted,13f).apply {visibility=View.GONE}
        // Developer tools stay hidden from learners: seven quick taps on the version show them.
        version.setOnClickListener {
            if(developerTools){hint.text="Developer tools are on.";hint.visibility=View.VISIBLE;return@setOnClickListener}
            val left=developerUnlock.tap(now())
            if(left==0){setDeveloper(true);openPage("about")}
            else if(left<=3){hint.text=if(left==1)"1 more tap for developer tools." else "$left more taps for developer tools.";hint.visibility=View.VISIBLE}
        }
        if(developerTools) {
            p.switch("Developer tools",true) {on->if(!on){setDeveloper(false);openPage("about")}}
            p.paragraph("Shown at the foot of Get ready: test sources, diagnostics and experiments.",CoachStyle.muted,13f)
        }
    }
    private fun setDeveloper(on:Boolean) {
        developerTools=on;flowPrefs.edit().putBoolean("developer",on).apply()
        if(!on)showLab=false
        updateSetupUi()
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
        options=Options(mode,speed,pitch,tti,physical.isChecked,braking.isChecked,manualCar,script,scriptStart,manualSpeed,slopeGuidance,demoGear,demoRpm,engineLoad.isChecked,filmedVideo.isChecked,videoPitch,phoneSlope.isChecked,
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
        audio.cancel();setup.stop();finishPractice(true);val previousId=core.calibrationId();core.reset();lastVision=null;lastCompletedVision=null
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
        if(::pretrained.isInitialized)pretrained.cancel()
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
        setup.stop();finishPractice(true)
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
    /** Caps the camera's frame rate (both the preview and the analysis stream) at `cap` frames a second;
     * 30 restores the camera's own choice. A frame-rate change is a capture-request option: no rebinding. */
    @androidx.annotation.OptIn(androidx.camera.camera2.interop.ExperimentalCamera2Interop::class)
    private fun applyFrameRateCap(cap:Int) {
        val bound=boundCamera?:return
        if(cap==appliedFpsCap)return
        appliedFpsCap=cap
        runCatching {
            val control=Camera2CameraControl.from(bound.cameraControl)
            if(cap>=30){control.clearCaptureRequestOptions();return@runCatching}
            val ranges=Camera2CameraInfo.from(bound.cameraInfo).getCameraCharacteristic(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
            val range=ranges?.filter {it.upper<=cap}?.maxWithOrNull(compareBy<android.util.Range<Int>>({it.upper},{it.lower}))?:return@runCatching
            control.setCaptureRequestOptions(CaptureRequestOptions.Builder()
                .setCaptureRequestOption(android.hardware.camera2.CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE,range).build())
            Log.i("LearnDrivePerf","camera frame rate capped at $range")
        }.onFailure {Log.w("LearnDriveVision","Camera frame-rate cap unavailable",it)}
    }
    /** A 120 Hz screen redraws the camera preview and map twice as often as needed: ask for 60 Hz. */
    private fun preferSixtyHertz() {
        runCatching {
            @Suppress("DEPRECATION") val display=if(android.os.Build.VERSION.SDK_INT>=30)this.display else windowManager.defaultDisplay
            val current=display?.mode?:return
            val sixty=display.supportedModes.filter {it.physicalWidth==current.physicalWidth&&it.physicalHeight==current.physicalHeight&&it.refreshRate>=59f}
                .minByOrNull {kotlin.math.abs(it.refreshRate-60f)}?:return
            if(sixty.refreshRate<current.refreshRate-1||window.attributes.preferredDisplayModeId!=sixty.modeId)
                window.attributes=window.attributes.apply {preferredDisplayModeId=sixty.modeId}
        }.onFailure {Log.w("LearnDrivePerf","60 Hz display mode unavailable",it)}
    }
    @androidx.annotation.OptIn(androidx.camera.camera2.interop.ExperimentalCamera2Interop::class)
    private fun startCamera(recoverSurface:Boolean=false) {
        // CameraX must receive an attached PreviewView surface. Starting from
        // onCreate/permission callbacks before the first layout can leave the
        // camera OPEN with no preview or analysis frames on some devices.
        if(!resumed||mode!="Camera")return
        if(!recoverSurface)cameraSurfaceRetries=0
        // Shown, not only sized: a view under a hidden (GONE) parent keeps its old size but is never
        // drawn, so its picture surface never arrives and CameraX gives up after 5 s (error 4).
        fun previewReady()=preview.isAttachedToWindow&&preview.isShown&&preview.width>0&&preview.height>0
        if(!previewReady()) {
            if(!cameraAwaitingLayout) {
                cameraAwaitingLayout=true
                val waitGeneration=++cameraPreviewWaitGeneration
                val deadline=now()+3.0
                Log.d("LearnDriveVision","Waiting for camera preview: attached=${preview.isAttachedToWindow}, shown=${preview.isShown}, size=${preview.width}x${preview.height}")
                main.post(object:Runnable {
                    override fun run() {
                        if(!cameraAwaitingLayout||waitGeneration!=cameraPreviewWaitGeneration)return
                        if(!resumed||mode!="Camera"||isDestroyed){cameraAwaitingLayout=false;return}
                        if(previewReady()) {
                            cameraAwaitingLayout=false
                            Log.d("LearnDriveVision","Camera preview ready: ${preview.width}x${preview.height}")
                            startCamera(recoverSurface)
                        } else {
                            // Late: say so, and keep checking slowly, so the camera starts by itself once
                            // its preview is on screen.
                            val late=now()>=deadline
                            if(late&&cameraError==null) {
                                cameraError="Waiting for the camera picture to appear on screen; the camera starts by itself."
                                Log.e("LearnDriveVision","Camera preview not ready: attached=${preview.isAttachedToWindow}, shown=${preview.isShown}, size=${preview.width}x${preview.height}")
                            }
                            main.postDelayed(this,if(late)500 else 32)
                        }
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
                // 1280x960 analysis frames (CameraX's default is 640x480). The detector still sees one
                // 320x320 image, but its 4x view of the far road is now the camera's own pixels: a person
                // 60 m ahead is ~23 px tall there instead of ~12 px in a 640-wide frame (0.8.3).
                val analysisSize=ResolutionSelector.Builder()
                    .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
                    .setResolutionStrategy(ResolutionStrategy(android.util.Size(1280,960),ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER))
                    .build()
                val analysis=ImageAnalysis.Builder().setTargetRotation(rotation).setResolutionSelector(analysisSize)
                    .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST).build().also {cameraAnalysisUseCase=it}
                analysis.setAnalyzer(inference) {image->
                    try {
                        if(token==generation.get()&&resumed) {
                            lastCameraFrameAt=now()
                            analysisTransform=imageTransform(image)
                            val thermal=if(android.os.Build.VERSION.SDK_INT>=29)getSystemService(PowerManager::class.java).currentThermalStatus else 0
                            cameraThermal=thermal
                            // Fewer camera frames once the phone is warm (the detector needs at most 8 a second).
                            val fpsCap=CameraVisionHealth.cameraFpsCap(thermal)
                            if(fpsCap!=appliedFpsCap)main.post {if(token==generation.get())applyFrameRateCap(fpsCap)}
                            if(CameraVisionHealth.shouldAnalyze(now(),lastRender,thermal)) {
                                analysisBusy=true;lastRender=now();model().thermalStatus=thermal
                                val result=model().frame(image,token.toLong(),sourceKind) { semantic ->
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
                boundCamera=bound;appliedFpsCap=30
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
                        val error=state.error
                        // The details go to the log; the learner gets what to do.
                        val retry=error?.code==androidx.camera.core.CameraState.ERROR_STREAM_CONFIG&&lastCameraFrameAt==0.0&&cameraSurfaceRetries<CAMERA_SURFACE_RETRIES
                        val text=error?.let {cameraErrorText(it.code,retry)}
                        if(error!=null&&text!=cameraError)Log.e("LearnDriveVision","Camera error ${error.code}, state ${state.type}",error.cause)
                        cameraError=text
                        cameraRunning=state.type==androidx.camera.core.CameraState.Type.OPEN
                        if(retry) {
                            // The picture surface was not ready in time: bind again, waiting a little longer each time.
                            val delay=300L*(1 shl 2*cameraSurfaceRetries)   // 0.3 s, 1.2 s, 4.8 s
                            cameraSurfaceRetries++
                            main.postDelayed({
                                if(resumed&&mode=="Camera"&&token==generation.get()&&lastCameraFrameAt==0.0)
                                    startCamera(recoverSurface=true)
                            },delay)
                        }
                        rules.post {evaluate()}
                    }
                }
                rules.post {evaluate()}
            }catch(e:Exception){cameraError="The camera did not start. Close other camera apps, then close this app fully and open it again.";Log.e("LearnDriveVision","Camera start failed",e)}
        },ContextCompat.getMainExecutor(this))
    }
    /** What a camera failure means for the learner, and what to do about it. */
    private fun cameraErrorText(code:Int,retrying:Boolean):String=when(code) {
        androidx.camera.core.CameraState.ERROR_MAX_CAMERAS_IN_USE,androidx.camera.core.CameraState.ERROR_CAMERA_IN_USE->
            "Another app is using the camera. Close it; this camera restarts by itself."
        androidx.camera.core.CameraState.ERROR_OTHER_RECOVERABLE_ERROR->"The camera stopped for a moment and is restarting."
        androidx.camera.core.CameraState.ERROR_STREAM_CONFIG->if(retrying)"The camera picture did not start. Trying again…"
            else "The camera picture did not start. Close this app fully and open it again."
        androidx.camera.core.CameraState.ERROR_CAMERA_DISABLED->"The camera is switched off on this phone (a privacy setting or device policy)."
        androidx.camera.core.CameraState.ERROR_CAMERA_FATAL_ERROR->"The camera stopped working. Restart the phone, then open this app again."
        androidx.camera.core.CameraState.ERROR_DO_NOT_DISTURB_MODE_ENABLED->"Do Not Disturb is blocking the camera. Turn it off, then try again."
        else->"The camera stopped. Close this app fully and open it again."
    }
    @androidx.annotation.OptIn(androidx.camera.view.TransformExperimental::class)
    private fun imageTransform(image:androidx.camera.core.ImageProxy)=runCatching {analysisTransforms.getOutputTransform(image)}.getOrNull()
    /** Analysed-frame pixels (crop, rotated) to preview pixels, or null until CameraX knows both. */
    @androidx.annotation.OptIn(androidx.camera.view.TransformExperimental::class)
    private fun previewMapping():android.graphics.Matrix? {
        val source=analysisTransform?:return null;val target=preview.outputTransform?:return null
        return runCatching {android.graphics.Matrix().also {androidx.camera.view.transform.CoordinateTransform(source,target).transform(it)}}.getOrNull()
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
        // Braking or a jolt never cuts an instruction off mid-word (Nov 6): the native engine
        // decides what may start and lets started speech finish its sentence.
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
        if(!core.taskSpeechAllowed())audio.cancelIfPrompt {it in 56..62}
        if(core.taskExecutionQuiet())audio.cancelRoutine()
        if(pretrained.canOffer&&t-lastReasoningSummary>=1.5){lastReasoningSummary=t;pretrained.offer(core.temporalContext(),cameraThermal)}
        if(delivery!=null) {
            // Audio can become available between sampled ticks. Preserve the
            // exact eligible decision before its acceptance acknowledgement.
            if(!decisionTraced)emitDecision()
            val acceptedAt=delivery.acceptedAt
            if(road&&setup.isMonitoring())practiceReview.accepted(acceptedAt,decision.origin,core.promptText(decision.prompt),delivery.voiceSubmitted)
            val presented=GuidanceTrace.coachingAccepted(decision,acceptedAt,delivery.voiceSubmitted,core.promptText(decision.prompt))
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
                    coachCaption.accept(core.promptText(decision.prompt),decision.sourceId,acceptedAt,audio.durationSeconds(decision.prompt),routeContext,decision.origin,decision.prompt)
            }
        }
    }
    private val ruleTick=object:Runnable {
        override fun run() {if(resumed){evaluate();traceBumps();rules.postDelayed(this,10)}}
    }
    /** Bump-detector events to diagnostics (no position); learned bumps saved every two minutes. */
    private fun traceBumps() {
        while(true) {
            val e=core.bumpEvent()?:break
            if(journal?.isAccepting==true)journal?.offer(GuidanceTrace.bumpEvent(e,core.bumpReason(e[2].toInt())))
        }
        if(now()-lastBumpSave>=120){lastBumpSave=now();saveLearnedBumps()}
    }
    private fun learnedBumpNote():String {
        if(!::learnedBumps.isInitialized||learnedBumps.count==0)return ""
        return " · ${learnedBumps.inUse} learned bumps"+if(learnedBumps.count>learnedBumps.inUse)" (+${learnedBumps.count-learnedBumps.inUse} to confirm)" else ""
    }
    private fun saveLearnedBumps() {
        if(!::learnedBumps.isInitialized)return
        evidenceIo.execute {runCatching {learnedBumps.save()}}
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
                    finishPractice(true)
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
            if(roadSource()&&setup.isMonitoring()){practiceReview.sample(visionNow,roadHealth.currentEvidence);driveFocusTick(visionNow,measured)}
            // Lab playback may retain a visibly delayed diagnostic result. The
            // road view must never put old green boxes over the current preview.
            overlay.result=when {
                mode=="Desk"->null
                roadSource()->if(roadHealth.currentEvidence)currentRoadOverlay(semantic?.result,freshSpatial) else null
                else->live
            }
            overlay.transform=if(roadSource())previewMapping() else null
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
            // A camera pointing at the sky sees little road: tell the driver (adjust only when parked).
            val mountAdvice=if(mode=="Camera"&&cameraRunning)core.cameraUp(visionNow).let {SpatialReadiness.mountAdvice(it[0],it[1],it[2])} else null
            // ...and only while it can be acted on: stopped or crawling. On the move this line says
            // what the camera is doing (on Nov 12 the advice replaced it for 13 of 14 minutes).
            val canAdjust=!measured.speedValid||!measured.speed.isFinite()||measured.speed<5
            // The camera's own state, on the camera: only when it is not simply working.
            val health=roadHealth.status
            status.setUiText(when {
                mode=="Desk"->"Simulated driving"
                mode=="MP4"||options.filmedVideo->"Recorded / screen test · basic detection"
                health.startsWith("Road detection active")->if("phone warm" in health)"Phone warm" else ""
                else->health
            })
            dashboard.setAimAdvice(mountAdvice?.takeIf {canAdjust&&roadSource()}?.let {when {
                "too high" in it->"Tilt camera down";"too low" in it->"Tilt camera up";"sideways" in it->"Level the camera";else->"Check camera aim"}})
            if(showLab||mode!="Camera")metrics.append("\n$visionStatus\n$warningStatus")
            dashboard.setMeasuredSpeed(if(roadSource()&&measured.speedValid)measured.speed else null,"GPS")
            if(visionNow-lastBatteryRead>=5) {
                lastBatteryRead=visionNow
                val battery=getSystemService(android.os.BatteryManager::class.java)
                dashboard.setBattery(battery?.getIntProperty(android.os.BatteryManager.BATTERY_PROPERTY_CAPACITY)?.takeIf {it in 0..100},battery?.isCharging==true)
            }
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
                // Without a destination the map still knows the road ahead: show it, and whether it loaded.
                val freePractice=roadSource()&&google?.state?.guiding!=true&&google?.state?.routeReady!=true
                val ahead=if(freePractice)core.roadJunction(visionNow) else null
                dashboard.navigationStatus.setUiText(if(!roadSource())"Navigation coaching paused for test input" else
                    if(freePractice)roadMapStatus.substringBefore(" (©")+learnedBumpNote() else google?.state?.status?:"Google navigation unavailable")
                val valid=roadSource()&&fix!=null&&update?.state==GoogleNavigationPhase.ENROUTE
                // A known maneuver with Google's distance; "continue"/unknown steps show no distance.
                val known=valid&&update?.maneuver!=GoogleManeuverKind.UNKNOWN&&update?.maneuver!=GoogleManeuverKind.STRAIGHT
                nextRoadSummary=when {
                    known&&update?.distanceMeters!=null->"Next: ${googleManeuverText(requireNotNull(update))} · ${distanceText(update.distanceMeters)}"
                    update?.state==GoogleNavigationPhase.REROUTING->"Google is finding a new route"
                    ahead!=null&&ahead.size>=9&&ahead[5]<=150->"Speed bump ahead · ${distanceText(ahead[5])}"
                    ahead!=null&&ahead.size>=9&&ahead[6]<=150->(if(ahead[7]!=0.0)"Sharp bend right" else "Sharp bend left")+" · ${distanceText(ahead[6])}"
                    ahead!=null&&ahead[0]!=0.0->"Junction ahead · ${distanceText(ahead[1])}"
                    else->null
                }
                // The turn card: Google's next step from its turn-by-turn feed (with "Google Maps"
                // attribution, as its terms require); otherwise what the map says about the road ahead.
                if(valid) {
                    val u=requireNotNull(update)
                    dashboard.setTurnCard(DriveCards.googleTurn(u.maneuver,u.distanceMeters,u.instruction,u.roadName,u.roundaboutExit,u.side,
                        u.nextManeuver,u.nextGapMeters,u.nextRoadName,google?.state?.destinationLabel))
                    dashboard.setTrip(u.destinationMeters,u.destinationSeconds);dashboard.setRoadName(u.currentRoad)
                } else {
                    val bump=ahead!=null&&ahead.size>=9&&ahead[5]<=80;val bend=!bump&&ahead!=null&&ahead.size>=9&&ahead[6]<=80
                    val junction=!bump&&!bend&&ahead!=null&&ahead[0]!=0.0
                    val text=when {
                        !roadSource()->"Test input"
                        google?.state?.routeReady==true&&!google.state.guiding->"Route ready · start the session"
                        update?.state==GoogleNavigationPhase.REROUTING->"Google is finding a new route"
                        google?.state?.guiding==true->"Waiting for a reliable location"
                        bump->"Speed bump ahead";bend->if(ahead!![7]!=0.0)"Sharp bend right ahead" else "Sharp bend left ahead"
                        junction->"Junction ahead";else->"Free practice"
                    }
                    val distance=when {bump->ahead!![5];bend->ahead!![6];junction->ahead!![1];else->null}
                    val icon=when {bump->"bump";bend->if(ahead!![7]!=0.0)"bendRight" else "bendLeft";junction->"location"
                        update?.state==GoogleNavigationPhase.REROUTING->"straight";else->"location"}
                    // Road data read from OpenStreetMap is credited where it is shown.
                    dashboard.setTurnCard(TurnCard(DriveCards.distance(distance),text,null,icon,null,null,
                        if(bump||bend||junction)"© OpenStreetMap" else null,false))
                    dashboard.setTrip(null,null);dashboard.setRoadName(null)
                }
            } else if(loadedRoute) {
                val navigation=core.navigationContext(visionNow)
                dashboard.navigationStatus.setUiText(if(roadSource())core.navigationStatus(visionNow) else "Map coaching paused for test input")
                val instruction=if(roadSource()&&navigation[10]!=0.0)routeInstructions[navigation[12].toInt()]?.takeIf {it.isNotBlank()} else null
                dashboard.setManeuver(instruction?:if(roadSource())"Waiting for route position" else "Test input",if(instruction!=null)navigation[13] else null)
                dashboard.setTrip(null,null);dashboard.setRoadName(null)
                if(journal?.isAccepting==true&&now()-lastNavigationTrace>=1) {
                    lastNavigationTrace=now();journal?.offer(GuidanceTrace.navigation(core.navigationContext(lastNavigationTrace),lastNavigationTrace))
                }
            }else {
                dashboard.navigationStatus.setUiText(routeLoadNotice?:availableMapName?.let {"$it · offline region"}?:"Add an offline region in settings")
                dashboard.setManeuver(if(routeLoadNotice!=null)"Route unavailable" else null)
                dashboard.setTrip(null,null);dashboard.setRoadName(null)
            }
            val captionActive=when(mode){"Camera"->cameraRunning&&(!roadSource()||setup.isMonitoring());"MP4"->videoRunning;else->true}
            // A late or failed camera clears only what the camera said; turn, hill and gear captions stay.
            if(mode=="Camera"&&issue!=null)coachCaption.clearCamera()
            val caption=coachCaption.presentation(visionNow,core.sourceId,captionActive,
                if(useGoogle||loadedRoute)core.navigationContext(visionNow) else null)
            if(visionNow-lastLessonUi>=.5){
                val state=org.json.JSONObject(core.driveContext());lessonUiPhase=state.optString("phase","WATCHING_ROAD")
                drivingTask=state.optString("task","OBSERVING");advisedGear=state.optInt("advised_gear",0);lastLessonUi=visionNow
            }
            val monitoringNow=roadSource()&&setup.isMonitoring()
            dashboard.setMapMode(!monitoringNow)
            // The gear that suits the measured speed: advice, shown only while the speed is measured.
            dashboard.setSuggestedGear(if(monitoringNow&&measured.speedValid&&advisedGear in 1..5)advisedGear else null)
            // The coach card: what was just said (its situation, the one thing to do, the rest), or
            // between instructions what is coming up, never an "all clear".
            dashboard.setCoachCard(when {
                caption!=null->if(caption.prompt>=1000)core.promptFragments(caption.prompt).takeIf {it.isNotEmpty()}
                    ?.let {DriveCards.fromFragments(it,caption.origin,caption.ageSeconds) {id->core.fragmentText(id)}}
                    ?:DriveCards.fromText(caption.text,caption.origin,caption.ageSeconds)
                    else DriveCards.fromText(caption.text,caption.origin,caption.ageSeconds)
                roadSource()&&!setup.isMonitoring()->DriveCards.parked("Your next session starts here.",
                    if(useGoogle&&googleNavigation?.state?.routeReady==true)"Route ready. Tap Get ready to start." else "Choose a destination, or practise without one.")
                // The starting checks and moving off, step by step, then the drive's focus while it is
                // briefed (an instruction takes the card, above).
                roadSource()&&openingCard(visionNow)!=null->openingCard(visionNow)!!
                roadSource()&&focusCard!=null&&visionNow<focusCardUntil->focusCard!!.let {DriveCards.focus(it.first,it.second)}
                // An instruction left unsaid because the learner slowed first is shown, never hidden:
                // the coach has seen the road user (silence is not an all-clear).
                roadSource()->core.learnerHold(visionNow).takeIf {it.size>=3&&it[0]>=0}?.let {DriveCards.held(LearnerReview.holdCaption(it[0].toInt()))}?:when(lessonUiPhase){
                    "WATCHING_ROAD","OBSERVING_RESPONSE"->DriveCards.idle(nextRoadSummary?:if(useGoogle&&googleNavigation?.state?.guiding==true)"Following Google's route" else "Free practice",roadHealth.idleCaption)
                    // The coach is not ready to instruct: say "waiting for GPS speed" only when
                    // the speed really is missing (a jolt or hard braking also pauses it briefly).
                    "EVIDENCE_UNAVAILABLE"->DriveCards.idle(if(core.snapshot(visionNow).speedValid)nextRoadSummary?:"Free practice"
                        else nextRoadSummary?.let {"$it · waiting for GPS speed"}?:"Waiting for GPS speed",roadHealth.idleCaption)
                    "EXECUTION_QUIET"->DriveCards.idle("Eyes on the road through the turn","")
                    "DELIVERING"->DriveCards.idle("Driving guidance",roadHealth.idleCaption)
                    else->DriveCards.idle(when(drivingTask){
                        "BEND"->"Preparing for the bend";"HILL"->"Adapting to the hill";"ROAD_USER_SPACE"->"Giving road users space"
                        "JUNCTION"->"Preparing for the junction";else->nextRoadSummary?:"Watching the road"},roadHealth.idleCaption)
                }
                mode=="Desk"->DriveCards.idle("Desk test · simulated driving","")
                mode=="MP4"->DriveCards.idle("Recorded video test","")
                options.filmedVideo->DriveCards.idle("Screen test · recorded scene only","")
                else->DriveCards.idle(roadHealth.idleCaption,"")
            })
            sampleTick()
            updateSetupUi()
            if(showLab||mode!="Camera")updateEvidenceUi()
            main.postDelayed(this,50)
        }
    }
    override fun onStart(){super.onStart();routeMap.onStart();googleNavigation?.onStart()}
    /** The drive screen's own strip shows the time and battery; the status bar returns with a swipe. */
    private fun hideStatusBar() {
        androidx.core.view.WindowCompat.getInsetsController(window,window.decorView).apply {
            systemBarsBehavior=androidx.core.view.WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(androidx.core.view.WindowInsetsCompat.Type.statusBars())
        }
    }
    override fun onWindowFocusChanged(hasFocus:Boolean){super.onWindowFocusChanged(hasFocus);if(hasFocus)hideStatusBar()}
    override fun onResume() {super.onResume();if(::phoneVoice.isInitialized&&phoneVoice.missingVoice&&voiceLanguage!=ai.learndrive.mobile.audio.VoicePhrases.ENGLISH)restartPhoneVoice();routeMap.onResume();googleNavigation?.onResume();resumed=true;loadOfflineRoute();if(::sensors.isInitialized)sensors.start();if(mode=="Camera"){inference.execute {visionFailure=null};startCamera()};main.post(tick);rules.post(ruleTick);main.post {startPendingLessonAudio()}}
    override fun onPause() {resumed=false;saveLearnedBumps();saveLearnerRecord();if(::pretrained.isInitialized)pretrained.pause();levelStart.cancel();sessionStartDialog?.dismiss();cameraAwaitingLayout=false;++cameraPreviewWaitGeneration;destinationPanel.setEditable(false);destinationPanel.setRunning(false);googleNavigation?.setEditable(false);googleNavigation?.stopGuidance();stopEvidence("backgrounded");if(evidenceStartPending){evidenceRequest.incrementAndGet();evidenceStartPending=false;evidenceBusy=false};finishPractice(true);setup.stop();cameraRunning=false;videoRunning=false;beginSource(captureSource());player?.close();camera?.unbindAll();sensors.close();audio.cancel();main.removeCallbacks(tick);rules.removeCallbacksAndMessages(null);routeMap.onPause();googleNavigation?.onPause();super.onPause()}
    override fun onStop(){routeMap.onStop();googleNavigation?.onStop();super.onStop()}
    override fun onSaveInstanceState(out:Bundle){routeMap.onSaveInstanceState(out);googleNavigation?.onSaveInstanceState(out);super.onSaveInstanceState(out)}
    override fun onLowMemory(){super.onLowMemory();if(::pretrained.isInitialized)pretrained.setEnabled(false);if(::routeMap.isInitialized)routeMap.onLowMemory();googleNavigation?.onLowMemory()}
    override fun onDestroy() {if(::phoneVoice.isInitialized)phoneVoice.close();appVoice?.close();if(::roads.isInitialized)roads.close();if(::pretrained.isInitialized)pretrained.close();getSystemService(android.hardware.display.DisplayManager::class.java).unregisterDisplayListener(displayListener);stopEvidence("activity_destroyed");googleNavigation?.close(isChangingConfigurations);googleNavigation=null;routeElevation?.close();routeElevation=null;destinationPanel.close();routeMap.close();evidenceIo.shutdown();audio.close();sensors.close();player?.close();ruleThread.quitSafely();inference.execute {vision?.close();core.close()};inference.shutdown();main.removeCallbacksAndMessages(null);super.onDestroy()}
}

// Choices kept in a learner's settings: the coach's voice ("voiceKind") and "All" for today's focus ("driveFocus").
private const val RECORDED_VOICE="recorded"
private const val PHONE_VOICE="phone"
private const val ALL_FOCUS="all"
