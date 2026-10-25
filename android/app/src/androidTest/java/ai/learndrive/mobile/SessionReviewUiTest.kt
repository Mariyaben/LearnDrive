package ai.learndrive.mobile

import android.app.AlertDialog
import android.app.KeyguardManager
import android.view.WindowManager
import android.view.PixelCopy
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeFalse
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** UI fixtures exercise review wiring, not a real drive or audible playback.
 * Native motion, GPS, camera evidence and session admission are never forged. */
@RunWith(AndroidJUnit4::class)
class SessionReviewUiTest {
    private fun field(name:String)=MainActivity::class.java.getDeclaredField(name).apply {isAccessible=true}
    private fun method(name:String)=MainActivity::class.java.getDeclaredMethod(name).apply {isAccessible=true}

    @Test fun completedCueReviewIsReadableAndSourceSwitchDiscardsIncompleteHistory() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        ActivityScenario.launch(MainActivity::class.java).use {scenario->
            scenario.onActivity {activity->
                assumeFalse("Unlock the phone for the review UI check",
                    activity.getSystemService(KeyguardManager::class.java).isKeyguardLocked)
                assumeFalse("Run the review UI check while stationary",method("knownMoving").invoke(activity) as Boolean)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                val history=field("practiceReview").get(activity) as PracticeSessionReview
                val t=now()-2
                // In-memory delivered-cue fixture only; this does not admit monitoring.
                assertTrue(history.start(t))
                assertTrue(history.accepted(t+.2,4,"Person ahead. Give them space."))
                history.sample(t+.25,true);history.sample(t+.4,true)
                method("endRoadSession").invoke(activity)
                val saved=field("lastPracticeReview").get(activity) as PracticeSessionReview.Review
                assertEquals(1,saved.roadCues)
                assertEquals(0,saved.navigationCues)
                val button=field("reviewSession").get(activity) as Button
                assertTrue("Completed review must be reachable from parked setup",button.isShown&&button.isEnabled)
                method("openSessionReview").invoke(activity)
                val dialog=field("sessionReviewDialog").get(activity) as AlertDialog
                if(dialog.getButton(AlertDialog.BUTTON_POSITIVE).text.toString()=="I'm parked") {
                    dialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
                }
            }
            // AlertDialog dispatches its button listener through the main queue.
            instrumentation.waitForIdleSync()
            // WindowManager focus can arrive after the main queue becomes idle.
            // Wait for the app-owned dialog; never copy an unfocused system surface.
            val focusDeadline=android.os.SystemClock.uptimeMillis()+5000
            var dialogFocused=false
            do {
                scenario.onActivity {a->
                    val dialog=field("sessionReviewDialog").get(a) as? AlertDialog
                    dialogFocused=dialog?.isShowing==true&&dialog.window?.decorView?.hasWindowFocus()==true
                }
                if(!dialogFocused)Thread.sleep(50)
            }while(!dialogFocused&&android.os.SystemClock.uptimeMillis()<focusDeadline)
            assertTrue("Review must receive window focus before capture",dialogFocused)
            val captured=CountDownLatch(1)
            var copyStatus=PixelCopy.ERROR_UNKNOWN
            lateinit var image:Bitmap
            scenario.onActivity {activity->
                val history=field("practiceReview").get(activity) as PracticeSessionReview
                val saved=field("lastPracticeReview").get(activity) as PracticeSessionReview.Review
                val dialog=field("sessionReviewDialog").get(activity) as AlertDialog
                assertTrue(dialog.isShowing)
                val message=dialog.findViewById<TextView>(android.R.id.message)
                assertNotNull(message)
                assertTrue("Review text must have visible laid-out bounds",message.isShown&&message.height>0)
                assertTrue(message.text.contains("Estimated camera availability"))
                assertTrue(message.text.contains("Person ahead. Give them space."))
                assertTrue(message.text.contains("Discuss with your instructor"))
                assertTrue(message.text.contains("not accuracy"))
                assertFalse((field("setup").get(activity) as DriveSetup).isMonitoring)
                val window=requireNotNull(dialog.window)
                assertTrue("LearnDrive review must be the focused window",window.decorView.hasWindowFocus())
                image=Bitmap.createBitmap(window.decorView.width,window.decorView.height,Bitmap.Config.ARGB_8888)
                PixelCopy.request(window,image,{status->copyStatus=status;captured.countDown()},Handler(Looper.getMainLooper()))
            }
            try {
                assertTrue("App dialog screenshot timed out",captured.await(5,TimeUnit.SECONDS))
                assertEquals(PixelCopy.SUCCESS,copyStatus)
                File(instrumentation.targetContext.filesDir,"session-review-ui.png").outputStream().use {
                    assertTrue(image.compress(android.graphics.Bitmap.CompressFormat.PNG,100,it))
                }
            } finally {image.recycle()}
            scenario.onActivity {activity->
                (field("sessionReviewDialog").get(activity) as AlertDialog).dismiss()
                val history=field("practiceReview").get(activity) as PracticeSessionReview
                val saved=field("lastPracticeReview").get(activity) as PracticeSessionReview.Review
                assertTrue(history.start(now()))
                assertTrue(history.accepted(now(),4,"Incomplete fixture"))
                MainActivity::class.java.getDeclaredMethod("switchMode",String::class.java).apply {isAccessible=true}
                    .invoke(activity,"Desk")
                assertNull("A source switch must not complete a road review",history.finish(now()))
                assertSame("Test input must not replace the previous completed lesson",saved,field("lastPracticeReview").get(activity))
            }
            instrumentation.waitForIdleSync()
        }
    }
}
