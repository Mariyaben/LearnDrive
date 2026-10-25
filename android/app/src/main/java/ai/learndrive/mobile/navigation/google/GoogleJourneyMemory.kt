package ai.learndrive.mobile.navigation.google

import android.os.Handler
import android.os.Looper
import androidx.lifecycle.ViewModel
import com.google.android.gms.maps.model.LatLng
import com.google.android.libraries.navigation.ListenableResultFuture
import com.google.android.libraries.navigation.Navigator
import com.google.android.libraries.navigation.Waypoint

/**
 * Same-process journey ownership across Activity configuration recreation. No SavedStateHandle,
 * files, preferences, Activity, View or controller is retained. SDK destinations are never replayed.
 */
class GoogleJourneyMemory:ViewModel() {
    internal data class Snapshot(val destinationLabel:String="",val routing:Boolean=false,val routeReady:Boolean=false,
        val status:String="Search for a destination, then start your supervised session",val error:Boolean=false)
    private val main=Handler(Looper.getMainLooper())
    private var navigator:Navigator?=null
    private var pending:ListenableResultFuture<Navigator.RouteStatus>?=null
    private var requestGeneration=0L
    private var observerOwner=0L
    private var observer:((Snapshot)->Unit)?=null
    internal var snapshot=Snapshot();private set
    var routeRequestCount=0L;private set

    internal fun owns(value:Navigator)=navigator===value

    /** Called after SDK acquisition. Presence is checked; a missing route is never re-requested. */
    internal fun adopt(value:Navigator) {
        val retained=navigator===value
        if(!retained) {
            ++requestGeneration;pending=null;navigator=value
            value.stopGuidance();value.clearDestinations();snapshot=Snapshot()
        }else {
            value.stopGuidance()
            if(snapshot.routeReady&&!hasSdkRoute(value))snapshot=snapshot.copy(routeReady=false,
                status="The previous Google route is unavailable. Choose the destination again.",error=true)
        }
    }

    internal fun observe(owner:Long,listener:(Snapshot)->Unit) {
        observerOwner=owner;observer=listener;listener(snapshot)
    }
    internal fun detach(owner:Long) {if(observerOwner==owner){observerOwner=0;observer=null}}

    internal fun prepare(label:String,position:LatLng) {
        val nav=navigator?:return
        val ticket=++requestGeneration
        nav.stopGuidance();nav.clearDestinations()
        snapshot=Snapshot(label.take(160),routing=true,status="Finding a Google route…");notifyChanged()
        try {
            val waypoint=Waypoint.builder().setLatLng(position.latitude,position.longitude).setTitle(label.take(160)).build()
            ++routeRequestCount
            pending=nav.setDestination(waypoint)
            // This callback captures only the ViewModel and SDK handle. An Activity may be
            // destroyed while routing; its successor observes the same completed request.
            pending!!.setOnResultListener {result->main.post {
                if(ticket!=requestGeneration||navigator!==nav)return@post
                pending=null
                if(result==Navigator.RouteStatus.OK) {
                    snapshot=snapshot.copy(routing=false,routeReady=true,status="Route ready · start your supervised session",error=false)
                }else {
                    nav.clearDestinations()
                    snapshot=snapshot.copy(routing=false,routeReady=false,status=routeError(result),error=true)
                }
                notifyChanged()
            }}
        }catch(_:Exception) {
            if(ticket==requestGeneration) {
                pending=null;snapshot=snapshot.copy(routing=false,routeReady=false,
                    status="Google could not request this route. Check location permission and connectivity.",error=true)
                notifyChanged()
            }
        }
    }

    internal fun clear() {
        ++requestGeneration;pending=null
        runCatching {navigator?.stopGuidance();navigator?.clearDestinations()}
        snapshot=Snapshot(status="Search for a destination");notifyChanged()
    }

    internal fun arrived() {
        ++requestGeneration;pending=null
        runCatching {navigator?.clearDestinations()}
        snapshot=snapshot.copy(routing=false,routeReady=false,status="Google reports arrival · check your destination",error=false)
        notifyChanged()
    }

    /** Final session end only. Configuration recreation must keep this owner alive. */
    internal fun release() {
        ++requestGeneration;pending=null;observerOwner=0;observer=null
        val nav=navigator;navigator=null;snapshot=Snapshot()
        runCatching {nav?.stopGuidance();nav?.clearDestinations();nav?.cleanup()}
    }
    override fun onCleared() {release();super.onCleared()}
    private fun notifyChanged() {observer?.invoke(snapshot)}
    private fun hasSdkRoute(nav:Navigator)=runCatching {nav.currentRouteSegment!=null||nav.routeSegments.isNotEmpty()}.getOrDefault(false)

    private fun routeError(result:Navigator.RouteStatus)=when(result) {
        Navigator.RouteStatus.LOCATION_DISABLED->"Turn on location services, then select the destination again."
        Navigator.RouteStatus.LOCATION_UNKNOWN->"Google could not get your location. Wait for a clear location fix and try again."
        Navigator.RouteStatus.NETWORK_ERROR->"Route unavailable without a connection. Try again online or choose Offline Kerala."
        Navigator.RouteStatus.NO_ROUTE_FOUND->"Google could not find a driving route to this destination."
        Navigator.RouteStatus.QUOTA_CHECK_FAILED->"Google routing quota is unavailable. Check the project's quota and billing."
        Navigator.RouteStatus.ROUTE_CANCELED->"Route request cancelled. Choose a destination when ready."
        else->"Google route unavailable (${result.name}). Choose another destination or try again."
    }
}
