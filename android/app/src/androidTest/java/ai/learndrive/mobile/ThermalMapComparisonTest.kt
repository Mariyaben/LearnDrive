package ai.learndrive.mobile

import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.PowerManager
import android.os.Process
import android.os.SystemClock
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import ai.learndrive.mobile.navigation.NavigationMapSurface
import ai.learndrive.mobile.navigation.OfflineNavigationMapSurface
import ai.learndrive.mobile.navigation.google.GoogleNavigationController
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import kotlin.math.acos
import kotlin.math.sqrt

/** Bench diagnostic, not a driving session. No source switching, fake GPS or policy bypass.
 * Records only counters/state: no images, audio, coordinates, destinations or SDK content.
 * ActivityScenario closes the camera/activity after completion or an aborted comparison. */
@RunWith(AndroidJUnit4::class)
class ThermalMapComparisonTest {
    private fun field(name:String)=MainActivity::class.java.getDeclaredField(name).apply {isAccessible=true}
    private fun activeMap(activity:MainActivity)=if(field("useGoogle").getBoolean(activity))
        field("googleNavigation").get(activity) as NavigationMapSurface else field("routeMap").get(activity) as NavigationMapSurface
    private fun rendered(map:NavigationMapSurface)=map.view.isShown&&when(map) {
        is GoogleNavigationController->map.isRendered
        is OfflineNavigationMapSurface->map.isRendered
        else->false
    }

    private data class Sample(val time:Double,val cpuMs:Long,val source:Long,val setup:Int,
        val thermal:Int,val plugged:Int,val powerSaver:Boolean,val foreground:Boolean,val camera:Boolean,val cameraAge:Double,
        val google:Boolean,val guiding:Boolean,val routeReady:Boolean,val session:Long?,val epoch:Long?,
        val suspended:Boolean,val mapRendered:Boolean,val mapWidth:Int,val mapHeight:Int,
        val up:DoubleArray,val width:Int,val height:Int,val rotation:Int,
        val vision:VisionPerformanceSnapshot) {
        fun json()=JSONObject().put("event","sample").put("elapsed_s",time).put("cpu_ms",cpuMs)
            .put("source",source).put("setup",setup).put("thermal",thermal).put("plugged",plugged).put("power_saver",powerSaver)
            .put("foreground",foreground).put("camera_running",camera).put("camera_age_s",cameraAge)
            .put("provider",if(google)"google" else "offline").put("guiding",guiding).put("route_ready",routeReady)
            .put("navigation_session",session?:JSONObject.NULL).put("route_epoch",epoch?:JSONObject.NULL)
            .put("renderer_suspended",suspended).put("map_rendered",mapRendered)
            .put("map_width",mapWidth).put("map_height",mapHeight).put("camera_up",org.json.JSONArray(up.toList()))
            .put("width",width).put("height",height).put("rotation",rotation)
            .put("vision_instance",vision.instanceId).put("vision_source",vision.sourceId)
            .put("capture_s",vision.lastCaptureTime).put("published_s",vision.publishedAt)
            .put("readiness",vision.lastReadiness).put("frames",vision.frames).put("frame_failures",vision.frameFailures)
            .put("frame_ms_total",vision.frameTotalMs).put("detector_calls",vision.detectorCalls)
            .put("detector_failures",vision.detectorFailures).put("detector_ms_total",vision.detectorTotalMs)
            .put("spatial_calls",vision.spatialCalls).put("spatial_native_completions",vision.spatialNativeCompletions)
            .put("spatial_failures",vision.spatialFailures).put("spatial_ms_total",vision.spatialTotalMs)
    }

    @Test fun mapOnOffOnPreservesCameraAndModelWorkload() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val context=instrumentation.targetContext
        val output=File(context.filesDir,"thermal-map-comparison.jsonl")
        val phaseSeconds=InstrumentationRegistry.getArguments().getString("phase_seconds")?.toIntOrNull()?.coerceIn(20,60)?:30
        output.bufferedWriter().use {writer->
            fun record(value:JSONObject) {writer.write(value.toString());writer.newLine();writer.flush()}
            record(JSONObject().put("event","header").put("version",BuildConfig.VERSION_NAME)
                .put("protocol","map-on/off/on; 10s settle then measured interval")
                .put("phase_seconds",phaseSeconds).put("sample_seconds",5).put("window_brightness",0.4)
                .put("constructor_benchmarks_in_counters",false).put("requires_completed_spatial_inference",true))
            try {
                assertEquals("Unplug the charger before launching the comparison",0,
                    context.registerReceiver(null,IntentFilter(Intent.ACTION_BATTERY_CHANGED))?.getIntExtra(BatteryManager.EXTRA_PLUGGED,-1)?:-1)
                assertEquals("Cool the phone to thermal status 0 before launching the comparison",0,
                    context.getSystemService(PowerManager::class.java).currentThermalStatus)
                assertFalse("Turn off Battery Saver before a normal-performance comparison",
                    context.getSystemService(PowerManager::class.java).isPowerSaveMode)
                ActivityScenario.launch(MainActivity::class.java).use {scenario->
                    fun sample():Sample {
                        var value:Sample?=null
                        val geometryDeadline=SystemClock.elapsedRealtime()+1_500
                        do {scenario.onActivity {activity->
                            val vision=(field("vision").get(activity) as? Vision)?.performanceSnapshot
                            val frame=(field("lastVision").get(activity) as? VisionResult)?.frame
                            // Semantic-first publications intentionally omit FrameContext. Wait for
                            // a completed camera result rather than inventing changed/zero geometry.
                            if(vision!=null&&vision.frames>0&&frame!=null) {
                                val core=field("core").get(activity) as NativeCore
                                val setup=field("setup").get(activity) as DriveSetup
                                val google=field("googleNavigation").get(activity) as? GoogleNavigationController
                                val useGoogle=field("useGoogle").getBoolean(activity)
                                val battery=context.registerReceiver(null,IntentFilter(Intent.ACTION_BATTERY_CHANGED))
                                val foreground=activity.lifecycle.currentState==androidx.lifecycle.Lifecycle.State.RESUMED&&
                                    activity.window.decorView.isShown&&!activity.getSystemService(android.app.KeyguardManager::class.java).isKeyguardLocked
                                value=Sample(now(),Process.getElapsedCpuTime(),core.sourceId,setup.state(),
                                    context.getSystemService(PowerManager::class.java).currentThermalStatus,
                                    battery?.getIntExtra(BatteryManager.EXTRA_PLUGGED,-1)?:-1,
                                    context.getSystemService(PowerManager::class.java).isPowerSaveMode,foreground,
                                    field("cameraRunning").getBoolean(activity),now()-field("lastCameraFrameAt").getDouble(activity),
                                    useGoogle,useGoogle&&google?.state?.guiding==true,useGoogle&&google?.state?.routeReady==true,
                                    if(useGoogle)google?.lastUpdate?.sessionId else null,if(useGoogle)google?.lastUpdate?.routeEpoch else null,
                                    activeMap(activity).isRenderingSuspendedForDiagnostics,rendered(activeMap(activity)),
                                    activeMap(activity).view.width,activeMap(activity).view.height,core.cameraUp(now()),
                                    frame.imageWidth,frame.imageHeight,frame.rotationDegrees,vision)
                            }
                        };if(value==null)Thread.sleep(10)}while(value==null&&SystemClock.elapsedRealtime()<geometryDeadline)
                        return requireNotNull(value){"No completed frame with camera geometry within 1.5 seconds"}
                    }
                    scenario.onActivity {activity->
                        activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        activity.window.attributes=activity.window.attributes.apply {screenBrightness=0.4f}
                    }
                    // Require real forward pose and a loaded visible map. Never accept SDK terms or
                    // force mount/calibration/session state to make a benchmark appear eligible.
                    val deadline=SystemClock.elapsedRealtime()+60_000
                    var ready=false
                    while(!ready&&SystemClock.elapsedRealtime()<deadline) {
                        assertTrue("Stopped during initialization at moderate thermal status",
                            context.getSystemService(PowerManager::class.java).currentThermalStatus<2)
                        scenario.onActivity {activity->
                            val vision=(field("vision").get(activity) as? Vision)?.performanceSnapshot
                            val map=activeMap(activity)
                            val mapReady=rendered(map)&&map.view.width>0
                            ready=vision?.lastReadiness==SpatialReadiness.READY&&vision.spatialNativeCompletions>0&&mapReady
                        }
                        if(!ready)Thread.sleep(500)
                    }
                    assertTrue("Need foreground camera with real forward pose and loaded map; keep phone upright and still",ready)
                    val baseline=sample()
                    record(baseline.json().put("phase","eligibility"))
                    assertEquals("Unplug the charger for this comparison",0,baseline.plugged)
                    assertEquals("Cool the phone to thermal status 0 before starting",0,baseline.thermal)
                    assertFalse("Battery Saver must stay off",baseline.powerSaver)
                    assertEquals("Use the live rear camera for this comparison","Camera",run {
                        var mode="";scenario.onActivity {mode=field("mode").get(it) as String};mode})
                    fun validate(current:Sample,suspended:Boolean) {
                        assertTrue("Phone must stay unlocked with LearnDrive visible",current.foreground)
                        assertTrue("Live camera must keep producing frames",current.camera&&current.cameraAge in 0.0..0.8)
                        assertEquals("Camera source changed",baseline.source,current.source)
                        assertEquals("Vision source changed",baseline.source,current.vision.sourceId)
                        assertEquals("Model instance changed",baseline.vision.instanceId,current.vision.instanceId)
                        assertEquals("Setup/monitoring state changed",baseline.setup,current.setup)
                        assertEquals("Navigation provider changed",baseline.google,current.google)
                        assertEquals("Guidance state changed",baseline.guiding,current.guiding)
                        assertEquals("Route availability changed",baseline.routeReady,current.routeReady)
                        assertEquals("Navigation session changed",baseline.session,current.session)
                        assertEquals("Route epoch changed",baseline.epoch,current.epoch)
                        assertEquals("Charger state changed",0,current.plugged)
                        assertFalse("Battery Saver activated; comparison stopped",current.powerSaver)
                        assertTrue("Stopped at moderate thermal status",current.thermal<2)
                        assertEquals("Map renderer state did not change as requested",suspended,current.suspended)
                        assertEquals("Map host bounds changed",listOf(baseline.mapWidth,baseline.mapHeight),listOf(current.mapWidth,current.mapHeight))
                        if(!suspended)assertTrue("An on-phase must show a loaded map",current.mapRendered)
                        assertEquals("Spatial readiness changed",SpatialReadiness.READY,current.vision.lastReadiness)
                        assertEquals("Camera geometry changed",listOf(baseline.width,baseline.height,baseline.rotation),listOf(current.width,current.height,current.rotation))
                        assertEquals("Frame processing failed",baseline.vision.frameFailures,current.vision.frameFailures)
                        assertEquals("Detector processing failed",baseline.vision.detectorFailures,current.vision.detectorFailures)
                        assertEquals("Spatial processing failed",baseline.vision.spatialFailures,current.vision.spatialFailures)
                        val a=baseline.up;val b=current.up
                        val norm=sqrt(a.sumOf {it*it}*b.sumOf {it*it})
                        assertTrue("Camera pose unavailable",norm>0)
                        val angle=Math.toDegrees(acos((a.indices.sumOf {a[it]*b[it]}/norm).coerceIn(-1.0,1.0)))
                        assertTrue("Camera tilt changed by more than 2 degrees",angle<=2.0)
                    }
                    val measured=mutableListOf<Pair<Sample,Sample>>()
                    var allSampledThermalsMatch=true
                    try {
                        for((label,suspended) in listOf("on_1" to false,"off" to true,"on_2" to false)) {
                            scenario.onActivity {activeMap(it).setRenderingSuspendedForDiagnostics(suspended)}
                            record(JSONObject().put("event","phase").put("phase",label).put("renderer_suspended",suspended).put("elapsed_s",now()))
                            // Exclude stop/start and map redraw transients from measured CPU windows.
                            repeat(2) {Thread.sleep(5_000);val s=sample();record(s.json().put("phase",label).put("settling",true));validate(s,suspended)
                                allSampledThermalsMatch=allSampledThermalsMatch&&s.thermal==baseline.thermal}
                            val first=sample();record(first.json().put("phase",label).put("settling",false));validate(first,suspended)
                            var last=first
                            val end=SystemClock.elapsedRealtime()+phaseSeconds*1000L
                            while(SystemClock.elapsedRealtime()<end) {
                                Thread.sleep(minOf(5_000L,(end-SystemClock.elapsedRealtime()).coerceAtLeast(1)))
                                last=sample();record(last.json().put("phase",label).put("settling",false));validate(last,suspended)
                                allSampledThermalsMatch=allSampledThermalsMatch&&last.thermal==baseline.thermal
                            }
                            assertTrue("Detector must run in every measured phase",last.vision.detectorCalls>first.vision.detectorCalls)
                            assertTrue("Native Supercombo must complete in every measured phase",last.vision.spatialNativeCompletions>first.vision.spatialNativeCompletions)
                            assertEquals("Spatial processing skipped some completed frames",
                                last.vision.frames-first.vision.frames,last.vision.spatialNativeCompletions-first.vision.spatialNativeCompletions)
                            measured.add(first to last)
                        }
                        // Successful collection alone does not establish a matched-workload comparison.
                        fun rates(count:(VisionPerformanceSnapshot)->Long)=measured.map {(a,b)->(count(b.vision)-count(a.vision))/(b.time-a.time)}
                        fun matched(values:List<Double>)=values.min()>0&&values.max()/values.min()<=1.2
                        val detectorMatched=matched(rates {it.detectorCalls})
                        val spatialMatched=matched(rates {it.spatialNativeCompletions})
                        val thermalConstant=allSampledThermalsMatch&&measured.all {(a,b)->a.thermal==baseline.thermal&&b.thermal==baseline.thermal}
                        record(JSONObject().put("event","complete").put("detector_rate_matched",detectorMatched)
                            .put("spatial_rate_matched",spatialMatched).put("all_sampled_thermals_matched",thermalConstant)
                            .put("matched_workload",detectorMatched&&spatialMatched&&thermalConstant)
                            .put("qualification","Short CPU comparison; no GPU watts or sustained thermal qualification"))
                    }finally {scenario.onActivity {activeMap(it).setRenderingSuspendedForDiagnostics(false)}}
                }
            }catch(error:Throwable) {
                record(JSONObject().put("event","aborted").put("reason",if(error is AssertionError)error.message else error.javaClass.simpleName))
                throw error
            }
        }
    }
}
