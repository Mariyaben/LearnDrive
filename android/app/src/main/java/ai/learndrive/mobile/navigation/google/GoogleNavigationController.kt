package ai.learndrive.mobile.navigation.google

import android.app.Activity
import android.content.ComponentCallbacks2
import android.content.res.Configuration
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import ai.learndrive.mobile.ui.CoachStyle
import ai.learndrive.mobile.navigation.DisplayPosition
import ai.learndrive.mobile.navigation.DisplayRoute
import ai.learndrive.mobile.navigation.NavigationMapSurface
import com.google.android.gms.maps.CameraUpdateFactory
import com.google.android.gms.maps.GoogleMap
import com.google.android.gms.maps.model.LatLng
import com.google.android.libraries.mapsplatform.turnbyturn.model.NavInfo
import com.google.android.libraries.mapsplatform.turnbyturn.model.NavState
import com.google.android.libraries.navigation.NavigationApi
import com.google.android.libraries.navigation.NavigationView
import com.google.android.libraries.navigation.Navigator
import com.google.android.libraries.places.api.Places
import java.util.concurrent.atomic.AtomicLong

/**
 * Owns only Google navigation/search UI and its ephemeral route context. It never owns the camera,
 * IMU, coaching audio or offline packs. All public methods/callbacks run on the main thread.
 * Constructing this class performs no SDK initialization, search, routing or terms acceptance.
 */
class GoogleNavigationController(private val activity:Activity,private val apiKey:String,
    private val onState:(GoogleNavigationState)->Unit={},
    private val onUpdate:(GoogleNavigationUpdate?)->Unit={},
    private val journey:GoogleJourneyMemory=GoogleJourneyMemory(),
    private val onDestinationSelected:()->Unit={}):NavigationMapSurface {
    private val main=Handler(Looper.getMainLooper())
    override val view=FrameLayout(activity).apply {setBackgroundColor(CoachStyle.ink)}
    private val placeholder=LinearLayout(activity).apply {
        orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER;setPadding(16,12,16,12)
    }
    private val placeholderText=TextView(activity).apply {
        text="Choose your route";textSize=18f;setTextColor(CoachStyle.text);gravity=Gravity.CENTER
    }
    private val enable=CoachStyle.button(activity,"Enable Google Maps",true).apply {setOnClickListener {enableFromUserAction()}}
    private val destination=GoogleDestinationSearch(activity,::routeTo,::clearDestination)
    val destinationView:View get()=destination.view
    var state=GoogleNavigationState();private set
    var lastUpdate:GoogleNavigationUpdate?=null;private set
    var isRendered=false;private set
    val routeRequestCount:Long get()=journey.routeRequestCount
    private var navigator:Navigator?=null
    private var navigationView:NavigationView?=null
    private var googleMap:GoogleMap?=null
    private var closed=false
    private var initializing=false
    // Retain Activity lifecycle intent while diagnostics stop only NavigationView rendering.
    private var started=false
    private var resumed=false
    private var renderingStarted=false
    private var renderingResumed=false
    private var pendingCamera:MapCameraAction?=null
    private var renderingSuspended=false
    override val isRenderingSuspendedForDiagnostics:Boolean get()=renderingSuspended
    private var interactive=true
    private var following=true
    private var editable=true
    private var focusAfterReady=false
    private var initialization=0L
    private var displayedDestinationLabel:String?=null
    private val ownerId=nextSessionId()
    private var sessionId=ownerId
    private var routeEpoch=1L
    private var sequence=0L
    private var feedRegistered=false
    private var lastFeedAt=Double.NEGATIVE_INFINITY
    private val routeChanged=Navigator.RouteChangedListener {
        // This listener invalidates old advice immediately. Only NavInfo.routeChanged
        // advances the epoch, avoiding a second revision for the same SDK change.
        if(!closed&&state.guiding)publishPhase(GoogleNavigationPhase.REROUTING)
    }
    private val rerouting=Navigator.ReroutingListener {
        if(!closed&&state.guiding) {
            publishPhase(GoogleNavigationPhase.REROUTING)
            updateState(state.copy(status="Google is finding a new route · turn coaching paused"))
        }
    }
    private val arrival=Navigator.ArrivalListener {
        if(!closed&&state.guiding) {
            stopGuidance()
            publishPhase(GoogleNavigationPhase.ARRIVED)
            journey.arrived()
        }
    }
    init {
        placeholder.addView(placeholderText,LinearLayout.LayoutParams(-1,-2));placeholder.addView(enable)
        placeholder.addView(TextView(activity).apply {
            text="Use your own judgment and follow actual road signs and conditions when they differ from the map. Routes may include toll roads and costs."
            textSize=12f;setTextColor(Color.LTGRAY);gravity=Gravity.CENTER;setPadding(0,6,0,0)
        },LinearLayout.LayoutParams(-1,-2))
        view.addView(placeholder,FrameLayout.LayoutParams(-1,-1))
    }

    /** Call only from an explicit parked user action. Google's own unmodified dialog handles consent. */
    fun enableFromUserAction() {initialize(allowTermsDialog=true)}

    /** A saved provider choice may restore the map only after Google previously recorded acceptance. */
    fun resumeIfPreviouslyAccepted() {
        if(closed||apiKey.isBlank())return
        if(runCatching {NavigationApi.areTermsAccepted(activity.application)}.getOrDefault(false))initialize(allowTermsDialog=false)
    }

    fun requestDestinationFocus() {
        if(state.ready)destination.requestFocus()
        else {focusAfterReady=true;enableFromUserAction()}
    }

    private fun initialize(allowTermsDialog:Boolean) {
        if(closed||initializing||state.ready)return
        if(apiKey.isBlank()) {fail("Google Maps is not configured. Add the restricted API key or choose Offline Kerala.");return}
        if(activity.isFinishing||activity.isDestroyed)return
        initializing=true;val ticket=++initialization;enable.isEnabled=false
        activeOwner=ownerId
        updateState(state.copy(status="Loading Google Maps · an internet connection is required",error=false))
        val callback=object:NavigationApi.NavigatorListener {
            override fun onNavigatorReady(value:Navigator) {
                if(closed||ticket!=initialization) {
                    // Navigator is a singleton. Do not tear down a newer in-flight
                    // initialization or another controller's live Navigator.
                    if(activeOwner==ownerId&&!initializing&&navigator!==value&&!journey.owns(value))runCatching {value.cleanup()}
                    return
                }
                initializing=false;enable.isEnabled=true
                try {
                    navigator=value
                    value.setAudioGuidance(Navigator.AudioGuidance.SILENT)
                    value.setTaskRemovedBehavior(Navigator.TaskRemovedBehavior.QUIT_SERVICE)
                    journey.adopt(value)
                    value.removeRouteChangedListener(routeChanged);value.removeReroutingListener(rerouting);value.removeArrivalListener(arrival)
                    value.addRouteChangedListener(routeChanged);value.addReroutingListener(rerouting);value.addArrivalListener(arrival)
                    attachMap()
                    if(!Places.isInitialized())Places.initializeWithNewPlacesApiEnabled(activity.applicationContext,apiKey)
                    destination.attach(Places.createClient(activity));destination.setEditable(editable)
                    journey.observe(ownerId) {snapshot->
                        if(closed)return@observe
                        if(displayedDestinationLabel!=snapshot.destinationLabel) {
                            displayedDestinationLabel=snapshot.destinationLabel;destination.restoreLabel(snapshot.destinationLabel)
                        }
                        updateState(state.copy(status=snapshot.status,ready=true,routing=snapshot.routing,
                            routeReady=snapshot.routeReady,destinationLabel=snapshot.destinationLabel,error=snapshot.error))
                        if(snapshot.routeReady)requestMapCamera(MapCameraAction.OVERVIEW)
                    }
                    if(focusAfterReady){focusAfterReady=false;destination.requestFocus()}
                }catch(_:Exception) {fail("Google Maps could not initialize. Check location permission, connection and project access.")}
            }
            override fun onError(errorCode:Int) {
                if(closed||ticket!=initialization)return
                initializing=false;enable.isEnabled=true
                fail(when(errorCode) {
                    NavigationApi.ErrorCode.TERMS_NOT_ACCEPTED->"Google terms were not accepted. Choose Google again when ready, or use Offline Kerala."
                    NavigationApi.ErrorCode.LOCATION_PERMISSION_MISSING->"Allow location permission, then enable Google Maps again."
                    NavigationApi.ErrorCode.NOT_AUTHORIZED->"The Google API key is not authorized. Check Navigation SDK access, billing and app restrictions."
                    else->"Google Maps is unavailable (code $errorCode). Check the connection and try again."
                })
            }
        }
        try {
            if(allowTermsDialog)NavigationApi.getNavigator(activity,callback)
            else NavigationApi.getNavigator(activity.application,callback)
        }catch(_:Exception) {initializing=false;enable.isEnabled=true;fail("Google Maps could not start. Check the app configuration and location permission.")}
        fun checkInitializationTimeout() {
            if(closed||!initializing||ticket!=initialization)return
            // Reading/accepting Google's own terms is a user action, not a network
            // timeout. Never dismiss or replace that dialog on a timer.
            if(allowTermsDialog&&!runCatching {NavigationApi.areTermsAccepted(activity.application)}.getOrDefault(false)) {
                main.postDelayed(::checkInitializationTimeout,20000);return
            }
            initializing=false;++initialization;enable.isEnabled=true;enable.text="Retry Google Maps"
            fail("Google Maps timed out. Check the connection and tap Retry, or choose Offline Kerala.")
        }
        main.postDelayed(::checkInitializationTimeout,20000)
    }

    private fun attachMap() {
        if(navigationView!=null)return
        val mapView=NavigationView(activity)
        isRendered=false
        navigationView=mapView
        mapView.onCreate(null)
        mapView.setForceNightMode(com.google.android.libraries.navigation.ForceNightMode.FORCE_NIGHT)
        syncRenderingLifecycle()
        // Retain provider maneuver/lane information along with attribution and map controls.
        mapView.setHeaderEnabled(true);mapView.setEtaCardEnabled(false)
        mapView.setSpeedometerEnabled(false);mapView.setSpeedLimitIconEnabled(true)
        mapView.setRecenterButtonEnabled(true)
        view.removeAllViews();view.addView(mapView,FrameLayout.LayoutParams(-1,-1))
        mapView.getMapAsync {map->
            if(closed)return@getMapAsync
            googleMap=map
            if(!isRenderingSuspendedForDiagnostics) {
                map.uiSettings.setAllGesturesEnabled(interactive)
                watchMapLoaded(map)
            }
            val camera=pendingCamera?:if(following)MapCameraAction.FOLLOW else null
            if(camera!=null)requestMapCamera(camera)
        }
    }

    private fun watchMapLoaded(map:GoogleMap) {
        map.setOnMapLoadedCallback {
            if(!closed&&googleMap===map&&!isRenderingSuspendedForDiagnostics)isRendered=true
        }
    }

    private fun requestMapCamera(action:MapCameraAction) {
        if(isRenderingSuspendedForDiagnostics) {pendingCamera=action;return}
        if(action==MapCameraAction.OVERVIEW) {
            pendingCamera=null;navigationView?.showRouteOverview();return
        }
        val map=googleMap?:return
        pendingCamera=null
        when(action) {
            MapCameraAction.FOLLOW->runCatching {map.followMyLocation(GoogleMap.CameraPerspective.TILTED)}
            MapCameraAction.FREEZE->map.moveCamera(CameraUpdateFactory.newCameraPosition(map.cameraPosition))
            MapCameraAction.OVERVIEW->navigationView?.showRouteOverview()
        }
    }

    fun setEditable(value:Boolean) {if(editable!=value){editable=value;destination.setEditable(value)}}
    fun setKeyboardVisible(visible:Boolean) {destination.setKeyboardVisible(visible)}

    private fun routeTo(label:String,position:LatLng) {
        if(navigator==null)return
        if(closed||!editable||!state.ready)return
        onDestinationSelected()
        stopGuidance();newSession();journey.prepare(label,position)
    }

    /** Root calls this only after its explicit mount/session confirmation. No automatic start or simulator. */
    fun startGuidance():Boolean {
        val nav=navigator?:return false
        if(closed||!state.routeReady||state.routing)return false
        if(state.guiding)return true
        try {
            newSession()
            GoogleNavigationFeed.attach(sessionId,::receive)
            feedRegistered=nav.registerServiceForNavUpdates(activity.packageName,GoogleNavigationUpdatesService::class.java.name,1)
            if(!feedRegistered) {
                GoogleNavigationFeed.detach(sessionId);fail("Google turn updates could not start. Navigation coaching is unavailable.");return false
            }
            lastFeedAt=now()
            updateState(state.copy(status="Starting Google navigation · waiting for current turn context",guiding=true,error=false))
            nav.setAudioGuidance(Navigator.AudioGuidance.SILENT);nav.startGuidance()
            publishPhase(GoogleNavigationPhase.UNKNOWN)
            if(following)requestMapCamera(MapCameraAction.FOLLOW)
            return true
        }catch(_:Exception) {
            stopGuidance();fail("Google navigation could not start. Check location permission and the connection.");return false
        }
    }

    fun stopGuidance() {
        if(closed)return
        if(state.guiding)updateState(state.copy(status=if(state.routeReady)"Route ready · session paused" else "Navigation stopped",guiding=false))
        GoogleNavigationFeed.detach(sessionId)
        if(feedRegistered)runCatching {navigator?.unregisterServiceForNavUpdates()}
        feedRegistered=false;runCatching {navigator?.stopGuidance()}
        publishPhase(GoogleNavigationPhase.STOPPED)
    }

    fun clearDestination() {
        if(closed)return
        stopGuidance();newSession();journey.clear()
    }

    private fun newSession() {
        sessionId=nextSessionId();routeEpoch=1;sequence=0
        lastFeedAt=Double.NEGATIVE_INFINITY;publishPhase(GoogleNavigationPhase.STOPPED)
    }

    private fun receive(info:NavInfo?,receivedAt:Double) {
        if(closed||!state.guiding)return
        lastFeedAt=receivedAt
        if(info==null){publishPhase(GoogleNavigationPhase.UNKNOWN);return}
        val phase=when(info.navState) {
            NavState.ENROUTE->GoogleNavigationPhase.ENROUTE
            NavState.REROUTING->GoogleNavigationPhase.REROUTING
            NavState.STOPPED->GoogleNavigationPhase.STOPPED
            else->GoogleNavigationPhase.UNKNOWN
        }
        if(phase==GoogleNavigationPhase.STOPPED) {
            stopGuidance();updateState(state.copy(status="Google navigation stopped",guiding=false));return
        }
        if(info.routeChanged)++routeEpoch
        val step=if(phase==GoogleNavigationPhase.ENROUTE)info.currentStep else null
        val update=GoogleNavigationUpdate(sessionId,routeEpoch,++sequence,receivedAt,phase,
            step?.stepNumber?.takeIf {it>=0},step?.let {normalizeGoogleManeuver(it.maneuver)}?:GoogleManeuverKind.UNKNOWN,
            if(step!=null)info.distanceToCurrentStepMeters?.takeIf {it>=0}?.toDouble() else null,
            if(step!=null)info.timeToCurrentStepSeconds?.takeIf {it>=0}?.toDouble() else null,
            step?.roundaboutTurnNumber?.takeIf {it>0})
        lastUpdate=update;onUpdate(update)
        updateState(state.copy(status=when(phase) {
            GoogleNavigationPhase.ENROUTE->if(step==null)"Google navigation active · turn information unavailable" else "Google navigation active"
            GoogleNavigationPhase.REROUTING->"Google is finding a new route · turn coaching paused"
            GoogleNavigationPhase.STOPPED->"Google navigation stopped"
            else->"Google turn information unavailable · turn coaching paused"
        },error=phase==GoogleNavigationPhase.UNKNOWN))
    }

    /** Time here is receive time, not a timestamp or accuracy supplied by Google. */
    fun tick(nowSeconds:Double) {
        if(!closed&&state.guiding&&lastUpdate?.state!=GoogleNavigationPhase.UNKNOWN&&
            (!nowSeconds.isFinite()||nowSeconds<lastFeedAt||nowSeconds-lastFeedAt>3)) {
            publishPhase(GoogleNavigationPhase.UNKNOWN)
            updateState(state.copy(status="Google turn updates delayed · turn coaching paused",error=true))
        }
    }

    private fun publishPhase(phase:GoogleNavigationPhase) {
        val update=GoogleNavigationUpdate(sessionId,routeEpoch,++sequence,now(),phase)
        lastUpdate=update;onUpdate(update)
    }
    private fun updateState(value:GoogleNavigationState) {
        if(closed)return
        state=value;placeholderText.text=value.status;destination.status(value.status);onState(value)
    }
    private fun fail(message:String,clearRoute:Boolean=false) {
        if(clearRoute){publishPhase(GoogleNavigationPhase.STOPPED)}
        updateState(state.copy(status=message,error=true,routing=false,
            routeReady=state.routeReady&&!clearRoute,guiding=state.guiding&&!clearRoute))
    }

    // Google owns its route and location display; never render OSM data on the Google map.
    override fun showRoute(route:DisplayRoute?)=Unit
    override fun updatePosition(position:DisplayPosition?)=Unit
    override fun setFollowing(enabled:Boolean) {
        following=enabled
        requestMapCamera(if(enabled)MapCameraAction.FOLLOW else MapCameraAction.FREEZE)
    }
    override fun setInteractive(enabled:Boolean) {
        interactive=enabled
        if(!isRenderingSuspendedForDiagnostics)googleMap?.uiSettings?.setAllGesturesEnabled(enabled)
    }
    override fun setRenderingSuspendedForDiagnostics(suspended:Boolean) {
        if(closed||isRenderingSuspendedForDiagnostics==suspended)return
        renderingSuspended=suspended
        if(suspended)isRendered=false
        else googleMap?.let {watchMapLoaded(it)}
        // Deliberately bypass this controller's onPause: it stops Navigator guidance.
        syncRenderingLifecycle()
        if(!suspended) {
            googleMap?.uiSettings?.setAllGesturesEnabled(interactive)
            pendingCamera?.let {requestMapCamera(it)}
        }
    }
    private fun syncRenderingLifecycle() {
        val mapView=navigationView?:return
        val shouldStart=started&&!isRenderingSuspendedForDiagnostics
        val shouldResume=shouldStart&&resumed
        if(renderingResumed&&!shouldResume){mapView.onPause();renderingResumed=false}
        if(renderingStarted&&!shouldStart){mapView.onStop();renderingStarted=false}
        mapView.visibility=if(isRenderingSuspendedForDiagnostics)View.INVISIBLE else View.VISIBLE
        if(!renderingStarted&&shouldStart){mapView.onStart();renderingStarted=true}
        if(!renderingResumed&&shouldResume){mapView.onResume();renderingResumed=true}
    }
    override fun onStart() {if(!closed&&!started){started=true;syncRenderingLifecycle()}}
    override fun onResume() {if(!closed&&!resumed){resumed=true;syncRenderingLifecycle()}}
    override fun onPause() {if(!closed&&resumed){stopGuidance();resumed=false;syncRenderingLifecycle()}}
    override fun onStop() {if(!closed&&started){if(resumed)onPause();started=false;syncRenderingLifecycle()}}
    override fun onSaveInstanceState(state:Bundle) {navigationView?.onSaveInstanceState(state)}
    override fun onLowMemory() {navigationView?.onTrimMemory(ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW)}
    fun onConfigurationChanged(configuration:Configuration) {navigationView?.onConfigurationChanged(configuration)}
    override fun close()=close(preserveJourney=false)
    fun close(preserveJourney:Boolean) {
        if(closed)return
        ++initialization;stopGuidance();journey.detach(ownerId);destination.close()
        if(resumed)onPause();if(started)onStop()
        runCatching {navigator?.removeRouteChangedListener(routeChanged);navigator?.removeReroutingListener(rerouting);navigator?.removeArrivalListener(arrival)}
        if(!preserveJourney)journey.release()
        navigationView?.onDestroy();navigationView=null;googleMap=null;navigator=null;isRendered=false;closed=true
        if(activeOwner==ownerId)activeOwner=0
        main.removeCallbacksAndMessages(null);view.removeAllViews()
    }
    private fun now()=SystemClock.elapsedRealtimeNanos()/1e9
    private enum class MapCameraAction { FOLLOW, FREEZE, OVERVIEW }
    companion object {
        private val sessions=AtomicLong()
        private var activeOwner=0L
        // Microseconds stay exact when route/session identifiers cross the existing double ABI.
        private fun nextSessionId()=sessions.updateAndGet {previous->maxOf(previous+1,SystemClock.elapsedRealtimeNanos()/1000)}
    }
}
