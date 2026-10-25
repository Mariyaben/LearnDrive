package ai.learndrive.mobile

import android.app.KeyguardManager
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.SystemClock
import android.view.FrameMetrics
import android.view.MotionEvent
import android.view.PixelCopy
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.widget.AutoCompleteTextView
import android.widget.Button
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Actual window, IME, touch dispatch and camera. A one-character draft causes no Places request.
 * Frame timings cover these parked interactions only, not sustained driving or thermal performance. */
@RunWith(AndroidJUnit4::class)
class DashboardInteractionTest {
    private fun field(name:String)=MainActivity::class.java.getDeclaredField(name).apply {isAccessible=true}
    @Test fun bothLandscapeOrientationsUpdateLiveCameraRotation() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        ActivityScenario.launch(MainActivity::class.java).use {scenario->
            fun waitFor(message:String,predicate:(MainActivity)->Boolean) {
                val end=SystemClock.uptimeMillis()+30_000;var ready=false
                do {scenario.onActivity {ready=predicate(it)};if(!ready)Thread.sleep(80)}while(!ready&&SystemClock.uptimeMillis()<end)
                assertTrue(message,ready)
            }
            var source=0L;var frameRotation=0;var before=0.0
            scenario.onActivity {a->
                assertFalse(a.getSystemService(KeyguardManager::class.java).isKeyguardLocked)
                assertEquals("Manifest permits both landscape directions",android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
                    a.packageManager.getActivityInfo(a.componentName,0).screenOrientation)
                a.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                a.requestedOrientation=android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            try {
                waitFor("First landscape orientation must produce live camera evidence") {a->
                    a.window.decorView.display.rotation==android.view.Surface.ROTATION_90&&field("lastCompletedVision").get(a)!=null
                }
                scenario.onActivity {a->
                    source=(field("core").get(a) as NativeCore).sourceId
                    val frame=(field("lastCompletedVision").get(a) as VisionResult).frame!!
                    frameRotation=frame.rotationDegrees;before=frame.captureTime
                    a.requestedOrientation=android.content.pm.ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE
                }
                waitFor("Reverse landscape must update real frame rotation, not only the UI") {a->
                    val frame=(field("lastCompletedVision").get(a) as? VisionResult)?.frame
                    a.window.decorView.display.rotation==android.view.Surface.ROTATION_270&&frame!=null&&frame.captureTime>before&&
                        frame.rotationDegrees==(frameRotation+180)%360
                }
                scenario.onActivity {a->
                    assertTrue(a.window.decorView.width>a.window.decorView.height)
                    assertEquals("A landscape flip keeps the same running capture source",source,(field("core").get(a) as NativeCore).sourceId)
                    assertFalse((field("setup").get(a) as DriveSetup).isMonitoring)
                }
                File(instrumentation.targetContext.filesDir,"ui-047-rotation-verification.json").writeText(JSONObject()
                    .put("manifest_sensor_landscape",true).put("both_display_rotations_checked",true)
                    .put("real_camera_rotation_changed_180_degrees",true).put("capture_source_preserved",true)
                    .put("road_session_started",false).toString(2))
            } finally {
                scenario.onActivity {it.requestedOrientation=android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE}
            }
        }
    }
    @Test fun parkedTouchFlowPreservesDraftCameraAndStickyAction() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val preferences=instrumentation.targetContext.getSharedPreferences("navigation_provider",android.content.Context.MODE_PRIVATE)
        val previous=if(preferences.contains("google"))preferences.getBoolean("google",true) else null
        preferences.edit().putBoolean("google",true).commit()
        val timings=Collections.synchronizedList(mutableListOf<Long>())
        val deadlines=Collections.synchronizedList(mutableListOf<Long>())
        val thread=HandlerThread("ui-frame-measurement").apply {start()}
        val listener=android.view.Window.OnFrameMetricsAvailableListener {_,m,_->
            timings.add(m.getMetric(FrameMetrics.TOTAL_DURATION))
            deadlines.add(m.getMetric(FrameMetrics.DEADLINE))
        }
        try {ActivityScenario.launch(MainActivity::class.java).use {scenario->
            fun root(a:MainActivity)=a.findViewById<ViewGroup>(android.R.id.content)
            fun search(a:MainActivity)=root(a).findViewWithTag<AutoCompleteTextView>("google-destination-search")
            fun waitUntil(message:String,timeout:Long=20_000,predicate:(MainActivity)->Boolean) {
                val end=SystemClock.uptimeMillis()+timeout;var okay=false
                do {scenario.onActivity {okay=predicate(it)};if(!okay)Thread.sleep(80)}while(!okay&&SystemClock.uptimeMillis()<end)
                assertTrue(message,okay)
            }
            fun ime(a:MainActivity)=ViewCompat.getRootWindowInsets(root(a))?.isVisible(WindowInsetsCompat.Type.ime())==true
            fun tap(tag:String) {
                var x=0f;var y=0f
                scenario.onActivity {a->
                    val v=root(a).findViewWithTag<View>(tag);val rect=Rect()
                    assertTrue("$tag must be visible and enabled",v.isShown&&v.isEnabled&&v.getGlobalVisibleRect(rect))
                    val p=IntArray(2);v.getLocationOnScreen(p);x=p[0]+v.width/2f;y=p[1]+v.height/2f
                }
                val down=SystemClock.uptimeMillis()
                instrumentation.sendPointerSync(MotionEvent.obtain(down,down,MotionEvent.ACTION_DOWN,x,y,0))
                instrumentation.sendPointerSync(MotionEvent.obtain(down,down+40,MotionEvent.ACTION_UP,x,y,0))
                instrumentation.waitForIdleSync()
            }
            fun capture(name:String) {
                val done=CountDownLatch(1);var status=-1;lateinit var image:Bitmap
                scenario.onActivity {a->
                    assertTrue("Only capture LearnDrive's focused window",a.window.decorView.hasWindowFocus())
                    val decor=a.window.decorView;image=Bitmap.createBitmap(decor.width,decor.height,Bitmap.Config.ARGB_8888)
                    PixelCopy.request(a.window,image,{status=it;done.countDown()},Handler(Looper.getMainLooper()))
                }
                try {
                    assertTrue(done.await(5,TimeUnit.SECONDS));assertEquals(PixelCopy.SUCCESS,status)
                    File(instrumentation.targetContext.filesDir,name).outputStream().use {assertTrue(image.compress(Bitmap.CompressFormat.PNG,100,it))}
                }finally {image.recycle()}
            }
            scenario.onActivity {a->
                assertFalse("Unlock the phone before this interaction test",a.getSystemService(KeyguardManager::class.java).isKeyguardLocked)
                assertFalse("Run this test while stationary",MainActivity::class.java.getDeclaredMethod("knownMoving").apply {isAccessible=true}.invoke(a) as Boolean)
                a.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
            waitUntil("Camera and Google destination entry must initialize",45_000) {a->
                field("lastCompletedVision").get(a)!=null&&search(a)?.isEnabled==true
            }
            var source=0L;var frame=0.0
            scenario.onActivity {a->
                source=(field("core").get(a) as NativeCore).sourceId;frame=field("lastCameraFrameAt").getDouble(a)
                val start=field("setupStart").get(a) as Button
                val bounds=Rect();assertTrue("Start remains visible without scrolling",start.getGlobalVisibleRect(bounds)&&bounds.height()==start.height)
                assertTrue("Start meets the 48 dp touch target",start.height>=48*a.resources.displayMetrics.density)
                a.window.addOnFrameMetricsAvailableListener(listener,Handler(thread.looper))
            }
            capture("ui-047-get-ready.png")
            tap("setup-close")
            waitUntil("Close setup should expose both persistent panels") {a->!root(a).findViewWithTag<View>("parked-setup-host").isShown}
            capture("ui-047-overview.png")
            repeat(3) {index->
                tap("destination-open")
                waitUntil("Where to must open the normal keyboard in place") {a->search(a).isShown&&search(a).hasFocus()&&ime(a)}
                waitUntil("Search must settle fully above the keyboard") {a->
                    val input=search(a);val rect=Rect()
                    input.getGlobalVisibleRect(rect)&&rect.height()==input.height&&
                        (field("navigationProviders").get(a) as View).visibility==View.GONE
                }
                scenario.onActivity {a->
                    val input=search(a)
                    assertTrue(input.imeOptions and EditorInfo.IME_FLAG_NO_EXTRACT_UI!=0)
                    assertTrue(input.imeOptions and EditorInfo.IME_FLAG_NO_FULLSCREEN!=0)
                    if(index==0){input.setText("Q",false);input.setSelection(1)}
                    else {assertEquals("Q",input.text.toString());assertEquals(1,input.selectionStart)}
                    val visible=Rect();assertTrue("Search must remain fully above the IME",input.getGlobalVisibleRect(visible)&&visible.height()==input.height)
                    assertEquals("The provider row should yield room to search",View.GONE,(field("navigationProviders").get(a) as View).visibility)
                    assertEquals(source,(field("core").get(a) as NativeCore).sourceId)
                }
                if(index==0)capture("ui-047-destination-entry.png")
                // A real Android Back dismisses the IME; the second closes the destination page.
                instrumentation.sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK)
                waitUntil("Back should dismiss the keyboard") {a->!ime(a)}
                scenario.onActivity {a->if(field("showDestination").getBoolean(a))a.onBackPressedDispatcher.onBackPressed()}
                waitUntil("Back returns to preparation with the sticky Start action") {a->
                    !field("showDestination").getBoolean(a)&&(field("setupStart").get(a) as View).isShown
                }
                if(index<2)tap("setup-close")
            }
            scenario.onActivity {a->
                assertTrue("Camera frames continue across real UI interaction",field("lastCameraFrameAt").getDouble(a)>frame)
                assertEquals(source,(field("core").get(a) as NativeCore).sourceId)
                assertFalse((field("setup").get(a) as DriveSetup).isMonitoring)
                a.window.removeOnFrameMetricsAvailableListener(listener)
            }
            val drained=CountDownLatch(1);Handler(thread.looper).post {drained.countDown()};assertTrue(drained.await(5,TimeUnit.SECONDS))
            val times=timings.toList().sorted();assertTrue("Frame timing samples must exist",times.isNotEmpty())
            fun percentile(p:Double)=times[((times.size-1)*p).toInt()]/1_000_000.0
            val budgetMisses=timings.indices.count {i->deadlines.getOrNull(i)?.let {it>0&&timings[i]>it}?:false}
            File(instrumentation.targetContext.filesDir,"ui-047-interaction-metrics.json").writeText(JSONObject()
                .put("interaction_cycles",3).put("real_touch_dispatch",true).put("real_ime",true)
                .put("camera_source_preserved",true).put("draft_and_cursor_preserved",true)
                .put("frame_samples",times.size).put("frame_total_p50_ms",percentile(.5))
                .put("frame_total_p95_ms",percentile(.95)).put("frame_total_max_ms",times.last()/1_000_000.0)
                .put("frames_exceeding_reported_deadline",budgetMisses)
                .put("scope","parked UI interactions with live camera; no sustained or baseline thermal comparison").toString(2))
        }} finally {
            thread.quitSafely()
            preferences.edit().apply {if(previous==null)remove("google") else putBoolean("google",previous)}.commit()
        }
    }
}
