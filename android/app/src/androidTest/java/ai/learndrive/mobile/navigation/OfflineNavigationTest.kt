package ai.learndrive.mobile.navigation

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import ai.learndrive.mobile.NativeCore
import ai.learndrive.mobile.CaptureSource
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class OfflineNavigationTest {
    @Test fun routeRunsLocallyWithPackAndRejectsMissingCoverage() {
        val instrumentation=InstrumentationRegistry.getInstrumentation();val context=instrumentation.targetContext
        val folder=File(context.cacheDir,"navigation-upstream-fixture");File(folder,"routing").mkdirs()
        instrumentation.context.assets.open("valhalla_tiles.tar").use {input->File(folder,"routing/tiles.tar").outputStream().use {input.copyTo(it)}}
        val pack=OfflinePack(folder,JSONObject().put("id","andorra-test").put("name","Andorra upstream fixture")
            .put("bounds",JSONArray(listOf(1.0,42.0,2.0,43.0))))
        val template=context.assets.open("navigation-valhalla-config.json").bufferedReader().use {it.readText()}
        OfflineRouter(pack,template).use {router->
            val first=router.route(42.5063,1.5218,42.5086,1.5394)
            File(context.filesDir,"andorra-runtime-route.json").writeText(first.json())
            assertTrue(first.shape.size>4);assertTrue(first.lengthKm>0)
            val second=router.route(42.5063,1.5218,42.5086,1.5394)
            assertEquals(first.lengthKm,second.lengthKm,.0001)
            println("NAVIGATION_BENCHMARK "+JSONObject().put("initialization_ms",router.initializationMs).put("warm_route_ms",router.routeMs)
                .put("length_km",second.lengthKm).put("points",second.shape.size/2)
                .put("device",android.os.Build.MODEL).put("sdk",android.os.Build.VERSION.SDK_INT)
                .put("fingerprint",android.os.Build.FINGERPRINT))
            NativeCore().use {core->
                core.beginSource(1,CaptureSource.CAMERA,0.0)
                assertTrue("Actual Valhalla shape must load into shared geometry",core.navigationRoute(first.id,first.shape,first.maneuvers))
                assertFalse(core.navigationContext(1.0)[10]!=0.0)
            }
            try {router.route(10.0,76.0,42.5086,1.5394);fail("Missing coverage accepted")}catch(expected:IllegalArgumentException){}
        }
    }
    @Test fun malformedPolylineRejected() {
        for(shape in listOf("~","????????????????????????????????~","\u0000")) {
            try {OfflineRoute.decodePolyline6(shape);fail("Malformed shape accepted")}catch(expected:IllegalArgumentException){}
        }
    }
}
