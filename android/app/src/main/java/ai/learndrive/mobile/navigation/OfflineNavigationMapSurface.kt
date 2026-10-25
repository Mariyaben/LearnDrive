package ai.learndrive.mobile.navigation

import android.content.Context
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.TextView
import androidx.core.view.doOnLayout
import org.json.JSONArray
import org.json.JSONObject
import org.maplibre.android.MapLibre
import org.maplibre.android.camera.CameraPosition
import org.maplibre.android.camera.CameraUpdateFactory
import org.maplibre.android.geometry.LatLng
import org.maplibre.android.geometry.LatLngBounds
import org.maplibre.android.maps.MapLibreMap
import org.maplibre.android.maps.MapLibreMapOptions
import org.maplibre.android.maps.MapView
import org.maplibre.android.maps.Style
import org.maplibre.android.style.sources.GeoJsonSource
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.roundToInt
import ai.learndrive.mobile.ui.CoachStyle

/** Local PMTiles only. No remote style, sprite, glyph endpoint, location engine or speech. */
class OfflineNavigationMapSurface(context: Context, savedState: Bundle? = null) : NavigationMapSurface {
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "map-display-route") }
    private val revision = AtomicInteger()
    private val root = FrameLayout(context)
    private val mapView: MapView
    private val notice = TextView(context).apply {
        text = "Add an offline region in settings"
        textSize = 14f
        setTextColor(CoachStyle.muted)
        setPadding(dp(12), dp(12), dp(12), dp(12))
        gravity = Gravity.CENTER
        setBackgroundColor(CoachStyle.ink)
    }
    private val recenter = Button(context).apply {
        text = "Follow"; textSize = 12f; isAllCaps = false
        contentDescription = "Follow measured GPS position"
        visibility = View.GONE
        setOnClickListener { setFollowing(true) }
    }
    private var map: MapLibreMap? = null
    private var style: Style? = null
    private var pack: OfflinePack? = null
    private var styleRevision = 0
    private var routeJson = EMPTY
    private var routeBounds: LatLngBounds? = null
    private var position: DisplayPosition? = null
    private var following = true
    private var interactive = true
    private var closed = false
    // Activity lifecycle intent is separate from the actual renderer lifecycle. A diagnostic
    // suspension must not change which lifecycle the display catches up to afterwards.
    private var started = false
    private var resumed = false
    private var renderingStarted = false
    private var renderingResumed = false
    private var packDisplayPending = false
    private var styleDisplayPending = false
    private var pendingCamera = PendingCamera.NONE
    private var renderingSuspended = false
    override val isRenderingSuspendedForDiagnostics: Boolean get() = renderingSuspended
    var isRendered = false
        private set
    override val view: View get() = root

    init {
        MapLibre.getInstance(context.applicationContext)
        mapView = MapView(context, MapLibreMapOptions.createFromAttributes(context)
            .textureMode(true).logoEnabled(false).attributionEnabled(false)
            .compassEnabled(false).setPrefetchesTiles(false).maxZoomPreference(18.0))
        mapView.onCreate(savedState?.getBundle(STATE_KEY))
        mapView.setMaximumFps(30)
        mapView.addOnDidFinishRenderingMapListener { complete ->
            isRendered = complete && style != null && pack != null && !closed && !isRenderingSuspendedForDiagnostics
        }
        root.setBackgroundColor(CoachStyle.ink)
        root.addView(mapView, FrameLayout.LayoutParams(-1, -1))
        root.addView(notice, FrameLayout.LayoutParams(-1, -2, Gravity.CENTER))
        root.addView(TextView(context).apply {
            text = "© OpenStreetMap contributors · Natural Earth"
            textSize = 10f
            setTextColor(CoachStyle.text)
            setPadding(dp(5), dp(4), dp(5), dp(4))
            setBackgroundColor(CoachStyle.surface)
            contentDescription = "Map data attribution: OpenStreetMap and Natural Earth"
        }, FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM or Gravity.START))
        root.addView(recenter, FrameLayout.LayoutParams(-2, dp(44), Gravity.BOTTOM or Gravity.END))
        mapView.addOnDidFailLoadingMapListener { error ->
            if (!closed) { notice.text = "Offline map unavailable: ${error.take(120)}"; notice.visibility = View.VISIBLE }
        }
        mapView.getMapAsync { ready ->
            if (closed) return@getMapAsync
            map = ready
            ready.addOnCameraMoveStartedListener { reason ->
                if (reason == MapLibreMap.OnCameraMoveStartedListener.REASON_API_GESTURE) {
                    following = false; updateFollowButton()
                }
            }
            applyPack()
        }
    }

    /** Call after the existing checked pack loader resolves a pack. Does not import or hash files. */
    fun showPack(value: OfflinePack?) {
        if (closed || pack?.id == value?.id) return
        pack = value
        position = position?.takeIf { value?.contains(it.latitude, it.longitude) == true }
        applyPack()
    }

    private fun applyPack() {
        if (isRenderingSuspendedForDiagnostics) {
            ++styleRevision // Ignore an older style's callback while retaining the latest pack.
            packDisplayPending = true
            return
        }
        val ready = map ?: run { packDisplayPending = true; return }
        packDisplayPending = false
        val selected = pack
        val token = ++styleRevision
        isRendered = false
        style = null
        notice.text = if (selected == null) "Add an offline region in settings" else "Loading ${selected.name} map…"
        notice.visibility = View.VISIBLE
        ready.setStyle(Style.Builder().fromJson(styleJson(selected))) { loaded ->
            if (closed || token != styleRevision) return@setStyle
            style = loaded
            finishStyleDisplay(selected, ready)
        }
    }

    private fun finishStyleDisplay(selected: OfflinePack?, ready: MapLibreMap) {
        if (isRenderingSuspendedForDiagnostics) { styleDisplayPending = true; return }
        styleDisplayPending = false
        pendingCamera = PendingCamera.NONE
        updateRouteSource()
        updatePositionSource()
        if (selected != null) {
            notice.visibility = View.GONE
            if (position != null && following) followPosition(false)
            else if (routeBounds != null) fitRoute()
            else ready.cameraPosition = CameraPosition.Builder().target(LatLng(
                (selected.bounds[1] + selected.bounds[3]) / 2,
                (selected.bounds[0] + selected.bounds[2]) / 2)).zoom(7.0).build()
        }
        setInteractive(interactive)
    }

    private fun updateRouteSource() {
        if (!isRenderingSuspendedForDiagnostics)
            style?.getSourceAs<GeoJsonSource>(ROUTE_SOURCE)?.setGeoJson(routeJson)
    }

    override fun showRoute(route: DisplayRoute?) {
        if (closed) return
        val ticket = revision.incrementAndGet()
        routeJson = EMPTY; routeBounds = null
        updateRouteSource()
        if (route == null) return
        // Serialization and bounds reduction stay off the camera/UI thread, including long routes.
        val shape = route.shape.copyOf()
        worker.execute {
            val prepared = runCatching {
                require(shape.size in 4..200000 && shape.size % 2 == 0)
                val coordinates = JSONArray()
                var north = -85.0; var south = 85.0; var east = -180.0; var west = 180.0
                for (i in shape.indices step 2) {
                    val lat = shape[i]; val lon = shape[i + 1]
                    require(lat.isFinite() && lon.isFinite() && lat in -85.0..85.0 && lon in -180.0..180.0)
                    coordinates.put(JSONArray().put(lon).put(lat))
                    north = maxOf(north, lat); south = minOf(south, lat)
                    east = maxOf(east, lon); west = minOf(west, lon)
                }
                val json = feature(JSONObject().put("type", "LineString").put("coordinates", coordinates))
                Pair(json, LatLngBounds.from(north, east, south, west))
            }
            main.post {
                if (closed || ticket != revision.get()) return@post
                val data = prepared.getOrNull()
                if (data == null) { notice.text = "Route display unavailable"; notice.visibility = View.VISIBLE; return@post }
                routeJson = data.first; routeBounds = data.second
                updateRouteSource()
                if (position == null || !following) fitRoute()
            }
        }
    }

    override fun updatePosition(position: DisplayPosition?) {
        if (closed) return
        val valid = position?.takeIf {
            val age = SystemClock.elapsedRealtimeNanos() / 1e9 - it.timeSeconds
            age in 0.0..5.0 && it.latitude.isFinite() && it.longitude.isFinite() &&
                it.latitude in -85.0..85.0 && it.longitude in -180.0..180.0 &&
                it.accuracyMetres.isFinite() && it.accuracyMetres in 0.0..50.0 &&
                pack?.contains(it.latitude, it.longitude) == true
        }
        if (valid == this.position) return
        this.position = valid
        updatePositionSource()
        if (valid != null && following) followPosition(true)
        updateFollowButton()
    }

    private fun updatePositionSource() {
        if (isRenderingSuspendedForDiagnostics) return
        val fix = position
        style?.getSourceAs<GeoJsonSource>(POSITION_SOURCE)?.setGeoJson(if (fix == null) EMPTY else
            feature(JSONObject().put("type", "Point").put("coordinates", JSONArray().put(fix.longitude).put(fix.latitude))))
    }

    override fun setFollowing(enabled: Boolean) {
        if (closed) return
        following = enabled
        if (enabled) followPosition(true)
        updateFollowButton()
    }

    private fun followPosition(animate: Boolean) {
        val fix = position ?: return
        val ready = map ?: return
        if (isRenderingSuspendedForDiagnostics) { pendingCamera = PendingCamera.FOLLOW; return }
        // North-up view: GPS position does not establish camera pose or reliable vehicle heading.
        val camera = CameraPosition.Builder().target(LatLng(fix.latitude, fix.longitude)).zoom(16.0).bearing(0.0).tilt(0.0).build()
        if (animate) ready.animateCamera(CameraUpdateFactory.newCameraPosition(camera), 350)
        else ready.moveCamera(CameraUpdateFactory.newCameraPosition(camera))
    }

    private fun fitRoute() {
        val bounds = routeBounds ?: return
        if (isRenderingSuspendedForDiagnostics) { pendingCamera = PendingCamera.ROUTE; return }
        if (root.width == 0 || root.height == 0) {
            root.doOnLayout { if (!closed && position == null) fitRoute() }
            return
        }
        map?.moveCamera(CameraUpdateFactory.newLatLngBounds(bounds, dp(24)))
    }

    override fun setInteractive(enabled: Boolean) {
        interactive = enabled
        if (!isRenderingSuspendedForDiagnostics) map?.uiSettings?.apply {
            isScrollGesturesEnabled = enabled
            isHorizontalScrollGesturesEnabled = enabled
            isZoomGesturesEnabled = enabled
            isDoubleTapGesturesEnabled = enabled
            isQuickZoomGesturesEnabled = enabled
            isRotateGesturesEnabled = false
            isTiltGesturesEnabled = false
        }
        updateFollowButton()
    }

    private fun updateFollowButton() { recenter.visibility = if (!following && position != null) View.VISIBLE else View.GONE }

    override fun setRenderingSuspendedForDiagnostics(suspended: Boolean) {
        if (closed || isRenderingSuspendedForDiagnostics == suspended) return
        renderingSuspended = suspended
        if (suspended) isRendered = false
        syncRenderingLifecycle()
        if (!suspended) {
            if (packDisplayPending) applyPack()
            else if (styleDisplayPending) map?.let { finishStyleDisplay(pack, it) }
            else {
                updateRouteSource(); updatePositionSource(); setInteractive(interactive)
                val camera = pendingCamera
                pendingCamera = PendingCamera.NONE
                when (camera) {
                    PendingCamera.FOLLOW -> if (following) followPosition(false)
                    PendingCamera.ROUTE -> fitRoute()
                    PendingCamera.NONE -> Unit
                }
            }
        }
    }

    private fun syncRenderingLifecycle() {
        val shouldStart = started && !isRenderingSuspendedForDiagnostics
        val shouldResume = shouldStart && resumed
        if (renderingResumed && !shouldResume) { mapView.onPause(); renderingResumed = false }
        // MapLibre's TextureView render thread is paused by onStop, not onPause.
        if (renderingStarted && !shouldStart) { mapView.onStop(); renderingStarted = false }
        mapView.visibility = if (isRenderingSuspendedForDiagnostics) View.INVISIBLE else View.VISIBLE
        if (!renderingStarted && shouldStart) { mapView.onStart(); renderingStarted = true }
        if (!renderingResumed && shouldResume) { mapView.onResume(); renderingResumed = true }
    }

    override fun onStart() { if (!closed && !started) { started = true; syncRenderingLifecycle() } }
    override fun onResume() { if (!closed && !resumed) { resumed = true; syncRenderingLifecycle() } }
    override fun onPause() { if (!closed && resumed) { resumed = false; syncRenderingLifecycle() } }
    override fun onStop() { if (!closed && started) { resumed = false; started = false; syncRenderingLifecycle() } }
    override fun onLowMemory() { if (!closed) mapView.onLowMemory() }
    override fun onSaveInstanceState(state: Bundle) {
        if (!closed) state.putBundle(STATE_KEY, Bundle().also { mapView.onSaveInstanceState(it) })
    }
    override fun close() {
        if (closed) return
        onPause(); onStop(); closed = true
        revision.incrementAndGet(); styleRevision++
        worker.shutdownNow(); main.removeCallbacksAndMessages(null)
        mapView.onDestroy(); map = null; style = null
    }

    private fun styleJson(selected: OfflinePack?): String {
        val sources = JSONObject().put(ROUTE_SOURCE, JSONObject().put("type", "geojson").put("data", JSONObject(EMPTY)))
            .put(POSITION_SOURCE, JSONObject().put("type", "geojson").put("data", JSONObject(EMPTY)))
        val layers = JSONArray().put(JSONObject().put("id", "background").put("type", "background")
            .put("paint", JSONObject().put("background-color", "#0b1822")))
        if (selected != null) {
            sources.put("basemap", JSONObject().put("type", "vector")
                .put("url", "pmtiles://${Uri.fromFile(selected.map)}")
                .put("attribution", "© OpenStreetMap contributors · Natural Earth"))
            val protomaps = selected.metadata.getString("map_schema") == "protomaps"
            fun fill(id: String, sourceLayer: String, color: String, minZoom: Int = 0) {
                layers.put(JSONObject().put("id", id).put("type", "fill").put("source", "basemap")
                    .put("source-layer", sourceLayer).put("minzoom", minZoom)
                    .put("paint", JSONObject().put("fill-color", color)))
            }
            fun line(id: String, sourceLayer: String, color: String, width: JSONArray) {
                layers.put(JSONObject().put("id", id).put("type", "line").put("source", "basemap")
                    .put("source-layer", sourceLayer).put("layout", JSONObject().put("line-join", "round").put("line-cap", "round"))
                    .put("paint", JSONObject().put("line-color", color).put("line-width", width)))
            }
            if (protomaps) fill("land", "earth", "#182c39")
            fill("parks", "landuse", "#213a3a")
            fill("water", "water", "#0c263b")
            fill("buildings", if (protomaps) "buildings" else "building", "#2a3d4b", 13)
            val roadLayer = if (protomaps) "roads" else "transportation"
            line("road-casing", roadLayer, "#0e202e", JSONArray("[\"interpolate\",[\"linear\"],[\"zoom\"],6,0.5,12,3,16,10]"))
            line("roads", roadLayer, "#6b8799", JSONArray("[\"interpolate\",[\"linear\"],[\"zoom\"],6,0.3,12,1.4,16,6]"))
        }
        layers.put(JSONObject().put("id", "route-casing").put("type", "line").put("source", ROUTE_SOURCE)
            .put("layout", JSONObject().put("line-cap", "round").put("line-join", "round"))
            .put("paint", JSONObject().put("line-color", "#06334b").put("line-width", 9)))
        layers.put(JSONObject().put("id", "route").put("type", "line").put("source", ROUTE_SOURCE)
            .put("layout", JSONObject().put("line-cap", "round").put("line-join", "round"))
            .put("paint", JSONObject().put("line-color", "#3dd5f3").put("line-width", 5)))
        layers.put(JSONObject().put("id", "position").put("type", "circle").put("source", POSITION_SOURCE)
            .put("paint", JSONObject().put("circle-color", "#52cfff").put("circle-radius", 7)
                .put("circle-stroke-width", 3).put("circle-stroke-color", "#ffffff")))
        // Symbol labels intentionally omitted until a packaged glyph source exists. Nothing fetches fonts.
        return JSONObject().put("version", 8).put("name", "LearnDrive local map").put("sources", sources).put("layers", layers).toString()
    }

    private fun feature(geometry: JSONObject) = JSONObject().put("type", "FeatureCollection").put("features",
        JSONArray().put(JSONObject().put("type", "Feature").put("properties", JSONObject()).put("geometry", geometry))).toString()
    private fun dp(value: Int) = (value * root.resources.displayMetrics.density).roundToInt()

    companion object {
        private const val STATE_KEY = "learndrive.offline.map"
        private const val ROUTE_SOURCE = "learndrive-route"
        private const val POSITION_SOURCE = "learndrive-position"
        private const val EMPTY = "{\"type\":\"FeatureCollection\",\"features\":[]}"
    }

    private enum class PendingCamera { NONE, FOLLOW, ROUTE }
}
