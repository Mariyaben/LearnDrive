package ai.learndrive.mobile.navigation

import android.graphics.Bitmap
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import ai.learndrive.mobile.NativeCore
import ai.learndrive.mobile.CaptureSource
import ai.learndrive.mobile.MainActivity
import android.widget.AutoCompleteTextView
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Explicit provisioning test: requires the real Kerala pack in app external files. */
@RunWith(AndroidJUnit4::class)
class KeralaNavigationTest {
    @Test fun importRouteAndIntegrateWithCameraCoach() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val context=instrumentation.targetContext
        val source=File(context.getExternalFilesDir(null),"kerala.ldnav")
        assertTrue("Provision the Kerala pack before running this test",source.isFile)
        val importStart=System.nanoTime()
        val pack=OfflinePack.installed(context).firstOrNull {it.id=="kerala-20261019-v3"}
            ?:source.inputStream().use {OfflinePack.import(context,it)}
        val importMs=(System.nanoTime()-importStart)/1e6
        assertTrue(pack.map.length()>100_000_000)
        val searchStart=System.nanoTime();val index=DestinationIndex.load(pack)
        val searchLoadMs=(System.nanoTime()-searchStart)/1e6
        assertTrue("Kochi must be searchable",index.search("Kochi").isNotEmpty())
        assertTrue("Named landmarks must be searchable",index.search("Lulu").isNotEmpty())
        resultSearchChecks(index)
        val timings=JSONArray()
        for(query in listOf("kochi","lulu","thrissur","medical college")) {
            val started=System.nanoTime();val found=index.search(query)
            assertTrue(found.isNotEmpty());timings.put(JSONObject().put("query",query).put("ms",(System.nanoTime()-started)/1e6))
        }
        val entries=index.size;index.close()
        val template=context.assets.open("navigation-valhalla-config.json").bufferedReader().use {it.readText()}
        val result=JSONObject().put("device",android.os.Build.MODEL).put("sdk",android.os.Build.VERSION.SDK_INT)
            .put("fingerprint",android.os.Build.FINGERPRINT).put("pack_id",pack.id).put("import_or_existing_lookup_ms",importMs)
            .put("internet_permission",context.checkSelfPermission(android.Manifest.permission.INTERNET)==android.content.pm.PackageManager.PERMISSION_GRANTED)
            .put("destination_index_load_ms",searchLoadMs).put("destination_entries",entries).put("search_timings",timings)
        var actualRoute:OfflineRoute?=null
        OfflineRouter(pack,template).use {router->
            val route=router.route(9.9679032,76.2444378,9.4923271,76.3291487)
            actualRoute=route.copy(destinationLabel="Alappuzha")
            result.put("initialization_ms",router.initializationMs).put("first_route_ms",router.routeMs)
                .put("length_km",route.lengthKm).put("shape_points",route.shape.size/2)
            assertTrue(route.lengthKm in 50.0..70.0)
            val samples=ArrayList<Double>()
            repeat(20) {
                val again=router.route(9.9679032,76.2444378,9.4923271,76.3291487)
                assertEquals(route.lengthKm,again.lengthKm,.0001);samples.add(router.routeMs)
            }
            samples.sort();result.put("warm_p50_ms",(samples[9]+samples[10])/2).put("warm_max_ms",samples.last())
                .put("warm_samples_ms",JSONArray(samples))
            NativeCore().use {core->
                core.beginSource(1,CaptureSource.CAMERA,0.0)
                val start=System.nanoTime()
                assertTrue(core.navigationRoute(route.id,route.shape,route.maneuvers))
                result.put("geometry_load_ms",(System.nanoTime()-start)/1e6)
                assertEquals(17,core.navigationContext(1.0).size)
                assertEquals(0.0,core.navigationContext(1.0)[10],0.0)
            }
            try {router.route(51.5,-.1,51.51,-.11);fail("Outside coverage accepted")}catch(expected:IllegalArgumentException){}
        }
        File(context.filesDir,"kerala-runtime-verification.json").writeText(result.toString(2))
        println("KERALA_BENCHMARK $result")
        val saved=File(context.filesDir,"navigation/active-route.json")
        val previous=if(saved.isFile)saved.readBytes() else null
        val providerPreferences=context.getSharedPreferences("navigation_provider",android.content.Context.MODE_PRIVATE)
        val previousProvider=if(providerPreferences.contains("google"))providerPreferences.getBoolean("google",true) else null
        // This fixture qualifies the explicit local fallback in an online-capable app.
        providerPreferences.edit().putBoolean("google",false).commit()
        saved.delete()
        try {ActivityScenario.launch(MainActivity::class.java).use {scenario->
            fun field(name:String)=MainActivity::class.java.getDeclaredField(name).apply {isAccessible=true}
            val resumedField=field("resumed")
            val coreField=field("core")
            val frameField=field("lastCameraFrameAt")
            val visionField=field("lastVision")
            val metricsField=field("metrics")
            fun currentCore(activity:MainActivity)=coreField.get(activity) as NativeCore
            fun currentSearch(activity:MainActivity):AutoCompleteTextView {
                val search=activity.findViewById<android.view.ViewGroup>(android.R.id.content)
                    .findViewWithTag<AutoCompleteTextView>("destination-search")
                assertNotNull("Destination search is in the current camera activity",search)
                return requireNotNull(search)
            }
            fun foreground(activity:MainActivity):Boolean = resumedField.getBoolean(activity)&&
                // An app-owned autocomplete popup may own window focus.
                activity.window.decorView.isShown&&
                activity.getSystemService(android.os.PowerManager::class.java).isInteractive&&
                !activity.getSystemService(android.app.KeyguardManager::class.java).isKeyguardLocked
            // Never retain views or a NativeCore across Activity recreation.
            // The identity is retained only to detect a broken continuity check.
            var continuityActivity:MainActivity?=null
            fun onCurrent(action:(MainActivity)->Unit) {
                scenario.onActivity {activity->
                    activity.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    action(activity)
                }
            }
            fun onSession(action:(MainActivity)->Unit) {
                onCurrent {activity->
                    if(continuityActivity!==activity||!foreground(activity)) {
                        println("INTEGRATION_DIAGNOSTIC continuity_activity=${continuityActivity?.let {System.identityHashCode(it)}} current_activity=${System.identityHashCode(activity)} lifecycle=${activity.lifecycle.currentState} window_focus=${activity.hasWindowFocus()} foreground=${foreground(activity)}")
                    }
                    assertSame("Activity was recreated during capture-source continuity testing; rerun with the phone unlocked",continuityActivity,activity)
                    assertTrue("Activity left the foreground during capture-source continuity testing",foreground(activity))
                    action(activity)
                }
            }
            // Copy only this app's Window. A system screenshot could capture a
            // home/lock screen if foreground ownership changed during a failure.
            fun saveAppWindow(name:String,required:Boolean=false) {
                val completed=java.util.concurrent.CountDownLatch(1)
                var bitmap:Bitmap?=null
                var copied=false
                onCurrent {activity->
                    if(!foreground(activity)) {
                        println("INTEGRATION_DIAGNOSTIC screenshot_skipped=activity_not_foreground file=$name")
                        completed.countDown()
                    } else {
                        val decor=activity.window.decorView
                        if(decor.width<=0||decor.height<=0)completed.countDown() else {
                            val image=Bitmap.createBitmap(decor.width,decor.height,Bitmap.Config.ARGB_8888)
                            bitmap=image
                            android.view.PixelCopy.request(activity.window,image,{status->
                                copied=status==android.view.PixelCopy.SUCCESS&&foreground(activity)
                                if(!copied)println("INTEGRATION_DIAGNOSTIC screenshot_skipped=window_copy_or_foreground_changed status=$status file=$name")
                                completed.countDown()
                            },android.os.Handler(android.os.Looper.getMainLooper()))
                        }
                    }
                }
                val finished=completed.await(5,java.util.concurrent.TimeUnit.SECONDS)
                val image=bitmap
                if(finished&&copied&&image!=null) {
                    File(context.filesDir,name).outputStream().use {assertTrue(image.compress(Bitmap.CompressFormat.PNG,100,it))}
                }
                if(finished)image?.recycle()
                if(required)assertTrue("App must remain foreground while capturing $name",finished&&copied)
            }
            fun waitUntil(why:String,test:()->Boolean) {
                val deadline=System.nanoTime()+30_000_000_000L
                var passed=test()
                while(!passed&&System.nanoTime()<deadline){Thread.sleep(100);passed=test()}
                if(!passed) {
                    onCurrent {a->
                        println("INTEGRATION_DIAGNOSTIC activity=${System.identityHashCode(a)} lifecycle=${a.lifecycle.currentState} window_focus=${a.hasWindowFocus()} foreground=${foreground(a)}")
                        for(name in listOf("cameraError","visionError","cameraRunning","resumed","cameraStartedAt","cameraAwaitingLayout","mode")) {
                            println("INTEGRATION_DIAGNOSTIC $name=${field(name).get(a)}")
                        }
                        val search=currentSearch(a)
                        println("INTEGRATION_DIAGNOSTIC search_attached=${search.isAttachedToWindow} search_shown=${search.isShown} search_enabled=${search.isEnabled} search_focused=${search.hasFocus()} suggestions=${search.adapter.count}")
                    }
                    saveAppWindow("integrated-test-failure.png")
                }
                assertTrue(why,passed)
            }
            var ready=false
            var readyActivity:MainActivity?=null
            var foregroundSince=0L
            waitUntil("Unlock the phone: activity must remain in foreground before camera testing") {
                onCurrent {activity->
                    val now=System.nanoTime()
                    if(!foreground(activity)){readyActivity=null;foregroundSince=0L;ready=false}
                    else {
                        if(readyActivity!==activity){readyActivity=activity;foregroundSince=now}
                        ready=now-foregroundSince>=500_000_000L
                    }
                };ready
            }
            // Wait for camera publication, then prove adding a route keeps its source.
            var firstFrame=0.0
            waitUntil("Camera must remain active") {onCurrent {ready=foreground(it);firstFrame=frameField.getDouble(it)};ready&&firstFrame>0}
            var visionPublished=false
            waitUntil("Existing detection pipeline must publish model results") {
                onCurrent {visionPublished=foreground(it)&&visionField.get(it)!=null};visionPublished
            }
            var uiTicked=false
            waitUntil("Dashboard updates must continue") {
                onCurrent {uiTicked=foreground(it)&&(metricsField.get(it) as android.widget.TextView).text.isNotBlank()};uiTicked
            }
            var cameraSource=0L
            onCurrent {activity->
                assertTrue("App must remain foreground when capture-source continuity testing begins",foreground(activity))
                assertNotNull("Current activity must have a published model result",visionField.get(activity))
                continuityActivity=activity;firstFrame=frameField.getDouble(activity);cameraSource=currentCore(activity).sourceId
            }
            onSession {activity->
                activity.findViewById<android.view.ViewGroup>(android.R.id.content).findViewWithTag<android.view.View>("destination-open").performClick()
                val search=currentSearch(activity)
                assertTrue("Destination search must be enabled while parked",search.isEnabled)
                assertTrue("Current destination search must accept focus",search.requestFocus());search.setText("Kochi")
            }
            var suggestions=0
            waitUntil("Typed destination must produce suggestions") {onSession {suggestions=currentSearch(it).adapter.count};suggestions>0}
            onSession {currentSearch(it).showDropDown();assertEquals("Searching must not restart capture",cameraSource,currentCore(it).sourceId)}
            instrumentation.waitForIdleSync();Thread.sleep(300) // Let the dropdown transition paint before visual evidence.
            saveAppWindow("integrated-destination-search.png",required=true)
            onSession {currentSearch(it).apply {clearFocus();dismissDropDown()}}
            saved.parentFile!!.mkdirs();saved.writeText(actualRoute!!.json())
            onSession {a->MainActivity::class.java.getDeclaredMethod("loadOfflineRoute").apply {isAccessible=true}.invoke(a)}
            var routeAttached=false
            waitUntil("Route must enter the existing camera core") {onSession {routeAttached=currentCore(it).navigationContext(1.0)[0]==actualRoute!!.id.toDouble()};routeAttached}
            onSession {
                assertEquals("Route attachment must preserve camera/Supercombo source",cameraSource,currentCore(it).sourceId)
                currentSearch(it).apply {requestFocus();setText("Thrissur")}
            }
            waitUntil("Replacement destination must produce suggestions") {
                onSession {suggestions=currentSearch(it).adapter.count};suggestions>0
            }
            onSession {
                assertEquals("Drafting another destination must preserve the committed route",
                    actualRoute!!.id.toDouble(),currentCore(it).navigationContext(1.0)[0],0.0)
                assertEquals("Replacement search must preserve camera/Supercombo source",cameraSource,currentCore(it).sourceId)
                currentSearch(it).apply {clearFocus();dismissDropDown()}
            }
            var laterFrame=0.0
            waitUntil("Camera frames must continue after attaching route") {onSession {laterFrame=frameField.getDouble(it)};laterFrame>firstFrame}
            var mapRendered=false
            waitUntil("Local map must render beside the camera") {onSession {a->
                mapRendered=(MainActivity::class.java.getDeclaredField("routeMap").apply {isAccessible=true}.get(a) as OfflineNavigationMapSurface).isRendered
            };mapRendered}
            // UI fixture only: exercise the monitoring layout without a road drive.
            onSession {a->
                val setup=MainActivity::class.java.getDeclaredField("setup").apply {isAccessible=true}.get(a) as ai.learndrive.mobile.DriveSetup
                val t=android.os.SystemClock.elapsedRealtimeNanos()/1e9;val up=currentCore(a).cameraUp(t)
                assertTrue(setup.start(t,up[0],up[1],up[2]))
                MainActivity::class.java.getDeclaredMethod("updateSetupUi").apply {isAccessible=true}.invoke(a)
                val root=a.findViewById<android.view.ViewGroup>(android.R.id.content)
                val map=root.findViewWithTag<android.view.View>("navigation-map-host")
                val road=root.findViewWithTag<android.view.View>("road-camera-host")
                assertTrue("Map stays visible during monitoring",map.isShown&&map.width>0&&map.height>0)
                assertTrue("Road camera stays visible during monitoring",road.isShown&&road.width>0&&road.height>0)
                assertFalse("Parked setup closes during monitoring",root.findViewWithTag<android.view.View>("parked-setup-host").isShown)
                val mapRect=android.graphics.Rect();val roadRect=android.graphics.Rect()
                assertTrue(map.getGlobalVisibleRect(mapRect));assertTrue(road.getGlobalVisibleRect(roadRect))
                assertFalse("Map and road camera have separate visible regions",android.graphics.Rect.intersects(mapRect,roadRect))
            }
            instrumentation.waitForIdleSync();Thread.sleep(300) // Visibility assertions precede the next rendered frame.
            saveAppWindow("integrated-route-coach.png",required=true)
            val beforePause=laterFrame
            continuityActivity=null // Intentional lifecycle recovery is a separate phase.
            scenario.moveToState(androidx.lifecycle.Lifecycle.State.STARTED)
            scenario.moveToState(androidx.lifecycle.Lifecycle.State.RESUMED)
            waitUntil("Camera must recover after backgrounding") {
                onCurrent {ready=foreground(it);laterFrame=frameField.getDouble(it)};ready&&laterFrame>beforePause
            }
            waitUntil("Detection pipeline must recover after backgrounding") {
                onCurrent {visionPublished=foreground(it)&&visionField.get(it)!=null};visionPublished
            }
            val beforeRecreation=laterFrame
            scenario.recreate()
            waitUntil("New activity layout must start the camera after recreation") {
                onCurrent {ready=foreground(it);laterFrame=frameField.getDouble(it)};ready&&laterFrame>beforeRecreation
            }
            waitUntil("New activity must restore its detection pipeline and committed route") {
                onCurrent {activity->
                    ready=foreground(activity)&&visionField.get(activity)!=null&&
                        currentCore(activity).navigationContext(1.0)[0]==actualRoute!!.id.toDouble()
                };ready
            }
        }} finally {
            if(previous==null)saved.delete() else saved.writeBytes(previous)
            providerPreferences.edit().apply {if(previousProvider==null)remove("google") else putBoolean("google",previousProvider)}.commit()
        }
    }
    private fun resultSearchChecks(index:DestinationIndex) {
        val place=index.search("Kochi").first()
        val alias=place.copy(name="Trichur",aliases=listOf("Thrissur"))
        assertEquals("Trichur",DestinationIndex(listOf(alias)).search("Thrissur").single().name)
        assertTrue(index.search("no-matching-destination-918273645").isEmpty())
    }
}
