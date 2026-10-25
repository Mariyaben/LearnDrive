package ai.learndrive.mobile.navigation

import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.view.View
import android.widget.AutoCompleteTextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import ai.learndrive.mobile.MainActivity
import ai.learndrive.mobile.NativeCore
import ai.learndrive.mobile.navigation.google.GoogleNavigationController
import ai.learndrive.mobile.navigation.google.GoogleNavigationPhase
import com.google.android.libraries.navigation.NavigationApi
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Real SDK integration, after the human accepts Google's first-use terms. No simulator or mock GPS. */
@RunWith(AndroidJUnit4::class)
class GoogleDashboardTest {
    @Test fun realSearchRouteAndTurnFeedKeepCameraAlive() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val context=instrumentation.targetContext
        assumeTrue("Human must accept Google terms on the phone before this test",NavigationApi.areTermsAccepted(context.applicationContext as android.app.Application))
        val preferences=context.getSharedPreferences("navigation_provider",android.content.Context.MODE_PRIVATE)
        val previous=if(preferences.contains("google"))preferences.getBoolean("google",true) else null
        preferences.edit().putBoolean("google",true).commit()
        try {ActivityScenario.launch(MainActivity::class.java).use {scenario->
            fun field(name:String)=MainActivity::class.java.getDeclaredField(name).apply {isAccessible=true}
            fun google(a:MainActivity)=field("googleNavigation").get(a) as GoogleNavigationController
            fun core(a:MainActivity)=field("core").get(a) as NativeCore
            fun foreground(a:MainActivity)=a.lifecycle.currentState==androidx.lifecycle.Lifecycle.State.RESUMED&&
                a.window.decorView.isShown&&!a.getSystemService(android.app.KeyguardManager::class.java).isKeyguardLocked
            var latestStatus=""
            fun waitUntil(message:String,timeoutSeconds:Long=40,test:(MainActivity)->Boolean) {
                val deadline=System.nanoTime()+timeoutSeconds*1_000_000_000L
                var ready=false
                do {scenario.onActivity {a->latestStatus=google(a).state.status;ready=foreground(a)&&test(a)}
                    if(!ready)Thread.sleep(100)
                }while(!ready&&System.nanoTime()<deadline)
                assertTrue("$message; $latestStatus",ready)
            }
            fun search(a:MainActivity)=a.findViewById<android.view.ViewGroup>(android.R.id.content)
                .findViewWithTag<AutoCompleteTextView>("google-destination-search")
            fun screenshot(name:String) {
                val done=CountDownLatch(1);var image:Bitmap?=null;var okay=false
                scenario.onActivity {a->
                    assertTrue("Capture only LearnDrive's foreground window",foreground(a))
                    val decor=a.window.decorView
                    image=Bitmap.createBitmap(decor.width,decor.height,Bitmap.Config.ARGB_8888)
                    android.view.PixelCopy.request(a.window,image!!,{code->okay=code==android.view.PixelCopy.SUCCESS&&foreground(a);done.countDown()},Handler(Looper.getMainLooper()))
                }
                assertTrue(done.await(5,TimeUnit.SECONDS)&&okay)
                File(context.filesDir,name).outputStream().use {image!!.compress(Bitmap.CompressFormat.PNG,100,it)}
                image?.recycle()
            }
            waitUntil("Google map and camera models must initialize") {a->google(a).state.ready&&google(a).isRendered&&field("lastVision").get(a)!=null}
            var source=0L;var beforeFrame=0.0
            scenario.onActivity {a->
                a.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                source=core(a).sourceId;beforeFrame=field("lastCameraFrameAt").getDouble(a)
                a.findViewById<android.view.ViewGroup>(android.R.id.content).findViewWithTag<View>("destination-open").performClick()
                search(a).requestFocus();search(a).setText("Kochi",false)
            }
            waitUntil("Google suggestions must appear in the existing activity") {a->search(a).adapter.count>0}
            screenshot("google-destination-search.png")
            scenario.onActivity {a->
                val controller=google(a)
                val destination=controller.javaClass.getDeclaredField("destination").apply {isAccessible=true}.get(controller)
                val popup=destination.javaClass.getDeclaredField("predictionPopup").apply {isAccessible=true}.get(destination) as android.widget.PopupWindow
                assertTrue("Prediction popup must be visible",popup.isShowing)
                val content=popup.contentView
                assertTrue("Popup content must have visible bounds",content.width>0&&content.height>0)
                val point=IntArray(2);content.getLocationOnScreen(point)
                val visible=android.graphics.Rect();search(a).getWindowVisibleDisplayFrame(visible)
                val popupBounds=android.graphics.Rect(point[0],point[1],point[0]+content.width,point[1]+content.height)
                val inputBounds=android.graphics.Rect();search(a).getGlobalVisibleRect(inputBounds)
                assertTrue("Suggestions must stay above the keyboard",popupBounds.bottom<=visible.bottom)
                assertFalse("Suggestions must not obscure the text being entered",android.graphics.Rect.intersects(popupBounds,inputBounds))
                // Copy only this app-owned popup. Activity-window PixelCopy can omit popup windows.
                val image=Bitmap.createBitmap(content.width,content.height,Bitmap.Config.ARGB_8888)
                content.draw(android.graphics.Canvas(image))
                File(context.filesDir,"google-search-suggestions.png").outputStream().use {image.compress(Bitmap.CompressFormat.PNG,100,it)}
                image.recycle()
            }
            scenario.onActivity {a->
                assertEquals(source,core(a).sourceId)
                val input=search(a)
                input.onItemClickListener!!.onItemClick(null,null,0,input.adapter.getItemId(0))
            }
            waitUntil("A real Google route must be prepared from the phone's current location",60) {a->google(a).state.routeReady}
            scenario.onActivity {a->
                assertFalse("A committed destination returns to Get ready",field("showDestination").getBoolean(a))
                assertEquals("Destination attachment preserves capture source",source,core(a).sourceId)
                assertTrue("Camera keeps producing frames during search and routing",field("lastCameraFrameAt").getDouble(a)>beforeFrame)
                // Explicit bench SDK check, with PCM SILENT. This does not start learner coaching
                // or bypass the product's mount-confirmation flow.
                assertTrue(google(a).startGuidance())
            }
            waitUntil("Registered real Google turn feed must publish ENROUTE",45) {a->google(a).lastUpdate?.state==GoogleNavigationPhase.ENROUTE}
            scenario.onActivity {a->
                assertEquals(source,core(a).sourceId)
                val root=a.findViewById<android.view.ViewGroup>(android.R.id.content)
                val setup=field("showSetup");setup.setBoolean(a,false)
                MainActivity::class.java.getDeclaredMethod("updateSetupUi").apply {isAccessible=true}.invoke(a)
                assertTrue(root.findViewWithTag<View>("navigation-map-host").isShown)
                assertTrue(root.findViewWithTag<View>("road-camera-host").isShown)
            }
            screenshot("google-route-camera-dashboard.png")
            var routeRequestsBeforeRecreation=0L
            scenario.onActivity {a->
                google(a).stopGuidance()
                assertFalse(google(a).state.guiding)
                assertEquals(GoogleNavigationPhase.STOPPED,google(a).lastUpdate?.state)
                routeRequestsBeforeRecreation=google(a).routeRequestCount
            }
            scenario.recreate()
            waitUntil("Activity recreation must retain the prepared Google route and recover camera") {a->
                google(a).state.ready&&google(a).state.routeReady&&!google(a).state.guiding&&field("lastVision").get(a)!=null
            }
            scenario.onActivity {a->
                assertEquals("Recreation must not issue another route request",routeRequestsBeforeRecreation,google(a).routeRequestCount)
                google(a).clearDestination();assertFalse(google(a).state.routeReady)
            }
            File(context.filesDir,"google-dashboard-verification.json").writeText(JSONObject()
                .put("real_map_initialized",true).put("real_ui_autocomplete_passed",true)
                .put("real_route_request_passed",true).put("real_enroute_feed_received",true)
                .put("camera_source_preserved",true).put("camera_frames_continued",true)
                .put("route_retained_across_recreation",true).put("camera_recovered_after_recreation",true)
                .put("no_extra_app_route_request_on_recreation",true)
                .put("stop_and_clear_passed",true).put("sdk_simulator_used",false)
                .put("human_terms_precondition",true).put("bench_only_not_road_qualification",true)
                .put("contains_coordinates",false).put("contains_place_content",false).toString(2))
        }} finally {
            preferences.edit().apply {if(previous==null)remove("google") else putBoolean("google",previous)}.commit()
        }
    }
}
