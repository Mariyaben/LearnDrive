package ai.learndrive.mobile

import android.app.KeyguardManager
import android.graphics.Rect
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.view.inspector.WindowInspector
import android.widget.Button
import android.widget.ScrollView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.SdkSuppress
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeFalse
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutorService
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/** UI regression with explicitly synthetic camera-result publications. Native
 * GPS/motion/evidence is never injected, and monitoring is never approved.
 * A barrier pauses the real inference executor so intermediate publications
 * can be interleaved with a real button DOWN/UP deterministically. This does
 * not qualify camera acquisition, perception accuracy or driving behavior. */
@RunWith(AndroidJUnit4::class)
@SdkSuppress(minSdkVersion=29)
class MainActivitySetupTest {
    private fun field(name:String)=MainActivity::class.java.getDeclaredField(name).apply {isAccessible=true}
    private val completedField=runCatching {field("lastCompletedVision")}.getOrNull()
    private val refresh=MainActivity::class.java.getDeclaredMethod("updateSetupUi").apply {isAccessible=true}
    private val eligibility=MainActivity::class.java.getDeclaredMethod("canStartRoad").apply {isAccessible=true}
    private val movingEligibility=runCatching {
        MainActivity::class.java.getDeclaredMethod("canStartRoad",Boolean::class.javaPrimitiveType).apply {isAccessible=true}
    }.getOrNull()

    private fun complete(source:Long,readiness:Int=SpatialReadiness.READY,age:Double=.03):VisionResult {
        val capture=now()-age
        return VisionResult(capture,Double.POSITIVE_INFINITY,0.0,false,20.0,emptyList(),640,480,
            completedAt=capture+.02,
            frame=FrameContext(source,CaptureSource.CAMERA,capture,imageWidth=640,imageHeight=480,
                cropWidth=640,cropHeight=480),spatialReadiness=readiness)
    }
    private fun mountButton(id:Int):Button?=WindowInspector.getGlobalWindowViews().asSequence()
        .mapNotNull {it.findViewById<Button>(id)}.firstOrNull {
            it.isShown&&if(id==android.R.id.button1)it.text.toString() in listOf("Mount checked · start","Level ground · start","Calibrating…","Starting…") else it.text.toString()=="Back"
        }
    private fun bounds(view:View)=Rect().also {rect->
        view.getDrawingRect(rect)
        (view.rootView as android.view.ViewGroup).offsetDescendantRectToMyCoords(view,rect)
    }

    @Test fun semanticPublicationCannotCancelStartTapAndRealGuardsStillReject() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val entered=CountDownLatch(1);val release=CountDownLatch(1)
        ActivityScenario.launch(MainActivity::class.java).use {scenario->
            var originalCameraRunning=false;var originalThermal=0
            var originalCameraError:Any?=null;var originalVisionError:Any?=null
            var originalsSaved=false
            try {
                val deadline=SystemClock.elapsedRealtime()+30_000
                var audioReady=false
                while(!audioReady&&SystemClock.elapsedRealtime()<deadline) {
                    scenario.onActivity {activity->
                        assumeFalse("Unlock LearnDrive for its interaction regression",
                            activity.getSystemService(KeyguardManager::class.java).isKeyguardLocked)
                        activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        audioReady=(field("audio").get(activity) as AudioWorker).ready
                    }
                    if(!audioReady)Thread.sleep(50)
                }
                assertTrue("Bundled voice must initialize before testing the real start gate",audioReady)
                scenario.onActivity {activity->
                    val moving=MainActivity::class.java.getDeclaredMethod("knownMoving").apply {isAccessible=true}.invoke(activity) as Boolean
                    assumeFalse("Do the UI regression while stationary; do not change measured motion",moving)
                    originalCameraRunning=field("cameraRunning").getBoolean(activity)
                    originalThermal=field("cameraThermal").getInt(activity)
                    originalCameraError=field("cameraError").get(activity)
                    originalVisionError=field("visionError").get(activity)
                    originalsSaved=true
                    (field("inference").get(activity) as ExecutorService).execute {
                        entered.countDown();release.await(45,TimeUnit.SECONDS)
                    }
                }
                assertTrue("Inference barrier must be entered",entered.await(30,TimeUnit.SECONDS))
                scenario.onActivity {activity->
                    // These flags/results are scoped UI fixtures, not real camera or thermal evidence.
                    field("cameraRunning").setBoolean(activity,true)
                    field("cameraThermal").setInt(activity,0)
                    field("cameraError").set(activity,null);field("visionError").set(activity,null)
                    field("showSetup").setBoolean(activity,true)
                    val source=(field("generation").get(activity) as AtomicInteger).get().toLong()
                    assertEquals(source,(field("core").get(activity) as NativeCore).sourceId)
                    val first=complete(source)
                    field("lastVision").set(activity,first);completedField?.set(activity,first)
                    refresh.invoke(activity)
                    val start=field("setupStart").get(activity) as Button
                    assertTrue("Fresh complete evidence must enable the start control",start.isEnabled)
                    val scroll=field("rightPane").get(activity) as ScrollView
                    scroll.isSmoothScrollingEnabled=false
                    start.requestRectangleOnScreen(Rect(0,0,start.width,start.height),true)
                }
                instrumentation.waitForIdleSync()
                scenario.onActivity {activity->
                    val source=(field("generation").get(activity) as AtomicInteger).get().toLong()
                    val fresh=complete(source)
                    field("lastVision").set(activity,fresh);completedField?.set(activity,fresh)
                    refresh.invoke(activity)
                    val start=field("setupStart").get(activity) as Button
                    assertTrue(start.isShown);assertTrue(start.width>0&&start.height>0)
                    val text=start.text.toString();val before=bounds(start)
                    val scroll=field("rightPane").get(activity) as ScrollView
                    val scrollY=scroll.scrollY
                    val downTime=SystemClock.uptimeMillis()
                    fun touch(action:Int) {
                        val event=MotionEvent.obtain(downTime,SystemClock.uptimeMillis(),action,start.width/2f,start.height/2f,0)
                        try {assertTrue("Start must receive the touch event",start.dispatchTouchEvent(event))}finally {event.recycle()}
                    }
                    touch(MotionEvent.ACTION_DOWN)
                    // A real semantic-first publication has no FrameContext. It must not
                    // disable/relabel the control while the completed UI snapshot is fresh.
                    val semantic=fresh.copy(frame=null)
                    repeat(3) {
                        field("lastVision").set(activity,semantic)
                        refresh.invoke(activity)
                        assertTrue("Intermediate semantic publication cancelled a valid Start tap",start.isEnabled)
                        assertEquals(text,start.text.toString())
                        assertEquals(before,bounds(start))
                        assertEquals(scrollY,scroll.scrollY)
                    }
                    // Completed spatial readiness can also fluctuate between frames. The
                    // entry action must not relabel, blink or steal a held tap. Admission
                    // still uses the fresh frame at confirmation time.
                    for(readiness in listOf(SpatialReadiness.ORIGINAL_CALIBRATION_UNKNOWN,SpatialReadiness.MOUNT_UNSUPPORTED)) {
                        val changed=complete(source,readiness)
                        field("lastVision").set(activity,changed);completedField?.set(activity,changed)
                        refresh.invoke(activity)
                        assertTrue("Readiness changes must not disable the held entry action",start.isEnabled)
                        assertEquals("Readiness changes must not relabel Start under a finger",text,start.text.toString())
                        assertEquals(before,bounds(start))
                    }
                    val staleDuringTap=complete(source,age=.5)
                    field("lastVision").set(activity,staleDuringTap);completedField?.set(activity,staleDuringTap)
                    refresh.invoke(activity)
                    assertTrue("A stale frame should explain readiness after tapping, not cancel the entry tap",start.isEnabled)
                    assertFalse("Stale evidence still cannot admit monitoring",eligibility.invoke(activity) as Boolean)
                    val recovered=complete(source)
                    field("lastVision").set(activity,recovered);completedField?.set(activity,recovered)
                    refresh.invoke(activity)
                    assertTrue("UI fixture must stay inside the real 350ms freshness bound",now()-fresh.time<.35)
                    touch(MotionEvent.ACTION_UP)
                }
                instrumentation.waitForIdleSync()
                var dialogFound=false
                val dialogDeadline=SystemClock.elapsedRealtime()+3_000
                while(!dialogFound&&SystemClock.elapsedRealtime()<dialogDeadline) {
                    scenario.onActivity {dialogFound=mountButton(android.R.id.button1)!=null}
                    if(!dialogFound)Thread.sleep(20)
                }
                assertTrue("The held Start tap must open the app's mount confirmation",dialogFound)
                scenario.onActivity {activity->
                    val source=(field("generation").get(activity) as AtomicInteger).get().toLong()
                    val stale=complete(source,age=.5)
                    field("lastVision").set(activity,stale);completedField?.set(activity,stale)
                    requireNotNull(mountButton(android.R.id.button1)).performClick()
                    assertFalse("A stale confirmation must never start monitoring",(field("setup").get(activity) as DriveSetup).isMonitoring)
                    assertNotNull("A rejected confirmation must stay open with retry guidance",mountButton(android.R.id.button1))
                    requireNotNull(mountButton(android.R.id.button2)).performClick()
                }
                instrumentation.waitForIdleSync()
                scenario.onActivity {activity->
                    val source=(field("generation").get(activity) as AtomicInteger).get().toLong()
                    assertFalse("Cancel must disarm a confirmed start",(field("levelStart").get(activity) as LevelReferenceStart).isPending)
                    field("cameraRunning").setBoolean(activity,true)
                    field("cameraThermal").setInt(activity,0)
                    field("cameraError").set(activity,null);field("visionError").set(activity,null)
                    val eligible=complete(source)
                    field("lastVision").set(activity,eligible);completedField?.set(activity,eligible)
                    assertTrue("Each rejection needs an otherwise eligible positive control",eligibility.invoke(activity) as Boolean)
                    fun rejected(name:String,result:VisionResult,error:String?=null,thermal:Int=0) {
                        field("lastVision").set(activity,result);completedField?.set(activity,result)
                        field("cameraError").set(activity,error);field("cameraThermal").setInt(activity,thermal)
                        assertFalse(name,eligibility.invoke(activity) as Boolean)
                    }
                    rejected("Stale capture must remain blocked",complete(source,age=.5))
                    rejected("Old source must remain blocked",complete(source+1))
                    rejected("Unsupported mount must remain blocked",complete(source,SpatialReadiness.MOUNT_UNSUPPORTED))
                    rejected("Camera failure must remain blocked",complete(source),error="UI fixture camera unavailable")
                    rejected("Critical thermal status must remain blocked",complete(source),thermal=4)
                    field("cameraError").set(activity,null);field("cameraThermal").setInt(activity,0)
                    val fresh=complete(source);field("lastVision").set(activity,fresh);completedField?.set(activity,fresh)
                    assertFalse("Measured moving state must remain blocked",requireNotNull(movingEligibility).invoke(activity,true) as Boolean)
                    assertFalse((field("setup").get(activity) as DriveSetup).isMonitoring)
                    MainActivity::class.java.getDeclaredMethod("beginSource",CaptureSource::class.java).apply {isAccessible=true}
                        .invoke(activity,CaptureSource.CAMERA)
                    assertNull("A source transition must discard the completed UI snapshot",requireNotNull(completedField).get(activity))
                }
            }finally {
                // Always unblock shutdown/model cleanup, including when an assertion fails.
                release.countDown()
                scenario.onActivity {activity->
                    mountButton(android.R.id.button2)?.performClick()
                    if(originalsSaved) {
                        field("cameraRunning").setBoolean(activity,originalCameraRunning)
                        field("cameraThermal").setInt(activity,originalThermal)
                        field("cameraError").set(activity,originalCameraError)
                        field("visionError").set(activity,originalVisionError)
                    }
                }
            }
        }
    }
}
