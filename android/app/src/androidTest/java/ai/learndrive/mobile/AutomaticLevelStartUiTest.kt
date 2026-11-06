package ai.learndrive.mobile

import android.app.AlertDialog
import android.app.KeyguardManager
import android.view.WindowManager
import android.widget.Button
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutorService
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/** Explicit UI fixtures below never approve a road session or inject native motion/GPS.
 * The calibration request is cancelled before completion; automatic IMU calibration
 * after cancellation must not restore the human level-ground confirmation. */
@RunWith(AndroidJUnit4::class)
class AutomaticLevelStartUiTest {
    private fun field(name:String)=MainActivity::class.java.getDeclaredField(name).apply {isAccessible=true}
    private fun call(a:MainActivity,name:String)=MainActivity::class.java.getDeclaredMethod(name).apply {isAccessible=true}.invoke(a)
    @Test fun entryAlwaysExplainsReadinessAndNeverInfersLevelGround() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        ActivityScenario.launch(MainActivity::class.java).use {scenario->
            scenario.onActivity {a->
                assertFalse(a.getSystemService(KeyguardManager::class.java).isKeyguardLocked)
                assertFalse(call(a,"knownMoving") as Boolean)
                a.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                val start=field("setupStart").get(a) as Button
                assertEquals("Start session",start.text.toString())
                assertTrue("The entry action stays available while readiness is explained",start.isEnabled)
                assertTrue(start.performClick())
                val dialog=field("sessionStartDialog").get(a) as AlertDialog
                assertTrue(dialog.isShowing)
                val text=dialog.findViewById<TextView>(android.R.id.message).text.toString()
                assertTrue(text.contains("level ground"));assertTrue(text.contains("cannot verify"))
                val setup=field("setup").get(a) as DriveSetup
                assertFalse(setup.referencePending());assertEquals(0L,setup.referenceId())
                assertFalse((field("levelStart").get(a) as LevelReferenceStart).isPending)
                assertFalse(setup.isMonitoring)
                dialog.dismiss()
            }
        }
    }
    @Test fun confirmedAutomaticCalibrationCanBeCancelledWithoutLateStart() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val entered=CountDownLatch(1);val release=CountDownLatch(1)
        ActivityScenario.launch(MainActivity::class.java).use {scenario->
            try {
                val deadline=android.os.SystemClock.uptimeMillis()+20_000;var ready=false
                do {
                    scenario.onActivity {a->
                        assertFalse(a.getSystemService(KeyguardManager::class.java).isKeyguardLocked)
                        assertFalse(call(a,"knownMoving") as Boolean)
                        a.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        ready=(field("audio").get(a) as AudioWorker).ready&&(field("sensors").get(a) as Sensors).motionAvailable
                    }
                    if(!ready)Thread.sleep(50)
                }while(!ready&&android.os.SystemClock.uptimeMillis()<deadline)
                assertTrue("Real voice and motion sensors must initialize",ready)
                scenario.onActivity {a->(field("inference").get(a) as ExecutorService).execute {entered.countDown();release.await(20,TimeUnit.SECONDS)}}
                assertTrue(entered.await(10,TimeUnit.SECONDS))
                scenario.onActivity {a->
                    // UI capability fixture only. Do not inject motion or promote a native reference.
                    val source=(field("generation").get(a) as AtomicInteger).get().toLong();val t=now()
                    field("lastCompletedVision").set(a,VisionResult(t,Double.POSITIVE_INFINITY,0.0,false,20.0,emptyList(),640,480,
                        completedAt=t,frame=FrameContext(source,CaptureSource.CAMERA,t,imageWidth=640,imageHeight=480),spatialReadiness=SpatialReadiness.READY))
                    field("cameraRunning").setBoolean(a,true);field("cameraThermal").setInt(a,0)
                    field("cameraError").set(a,null);field("visionError").set(a,null)
                    (field("setupStart").get(a) as Button).performClick()
                }
                // AlertDialog delivers OnShow on the main queue. A second user tap occurs
                // after that dispatch; do not invoke the builder's temporary null listener.
                instrumentation.waitForIdleSync()
                scenario.onActivity {a->
                    val source=(field("generation").get(a) as AtomicInteger).get().toLong();val t=now()-.6
                    field("lastCompletedVision").set(a,VisionResult(t,Double.POSITIVE_INFINITY,0.0,false,20.0,emptyList(),640,480,
                        completedAt=t,frame=FrameContext(source,CaptureSource.CAMERA,t,imageWidth=640,imageHeight=480),spatialReadiness=SpatialReadiness.READY))
                    assertFalse("The stale fixture cannot admit monitoring",call(a,"canStartRoad") as Boolean)
                    val dialog=field("sessionStartDialog").get(a) as AlertDialog
                    assertEquals("Level ground · start",dialog.getButton(AlertDialog.BUTTON_POSITIVE).text.toString())
                    dialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
                    val setup=field("setup").get(a) as DriveSetup
                    assertTrue("Confirmation automatically requests a new reference",setup.referencePending())
                    assertEquals(0L,setup.referenceId())
                    assertTrue((field("levelStart").get(a) as LevelReferenceStart).isPending)
                    assertFalse("Calibration is not monitoring",setup.isMonitoring)
                    dialog.getButton(AlertDialog.BUTTON_NEGATIVE).performClick()
                }
                instrumentation.waitForIdleSync();release.countDown()
                scenario.onActivity {a->
                    assertFalse((field("levelStart").get(a) as LevelReferenceStart).isPending)
                    assertFalse((field("setup").get(a) as DriveSetup).referencePending())
                }
                Thread.sleep(6000)
                scenario.onActivity {a->
                    val setup=field("setup").get(a) as DriveSetup
                    assertFalse("Cancelling cannot silently start later",setup.isMonitoring)
                    assertEquals("Automatic sensor normalization cannot restore cancelled level ground",0L,setup.referenceId())
                    assertFalse(setup.levelAllowed((field("core").get(a) as NativeCore).calibrationId()))
                }
            }finally {release.countDown()}
        }
    }
}
