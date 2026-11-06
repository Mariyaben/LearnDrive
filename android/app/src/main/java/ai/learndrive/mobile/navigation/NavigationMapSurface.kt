package ai.learndrive.mobile.navigation

import android.os.Bundle
import android.view.View

/** Ephemeral display data; a provider must not persist another provider's route data through this API. */
data class DisplayRoute(val id: Long, val shape: DoubleArray)
data class DisplayPosition(val timeSeconds: Double, val latitude: Double, val longitude: Double,
    val accuracyMetres: Double)

/** Map display never owns capture, motion fusion, destination search or speech. Main-thread API. */
interface NavigationMapSurface : AutoCloseable {
    val view: View
    fun showRoute(route: DisplayRoute?)
    fun updatePosition(position: DisplayPosition?)
    fun setFollowing(enabled: Boolean)
    fun setInteractive(enabled: Boolean)
    /** Diagnostic comparison only: suspend the display, retaining guidance and its input streams. */
    val isRenderingSuspendedForDiagnostics: Boolean
    fun setRenderingSuspendedForDiagnostics(suspended: Boolean)
    fun onStart()
    fun onResume()
    fun onPause()
    fun onStop()
    fun onSaveInstanceState(state: Bundle)
    fun onLowMemory()
}
