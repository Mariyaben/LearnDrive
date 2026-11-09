package ai.learndrive.mobile.navigation.google

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.cos
import kotlin.math.hypot

/** What the app sends to the Elevation API: points every 20 m along the route, as Google's polyline. */
class RouteElevationTest {
    private fun metres(a:DoubleArray,i:Int,j:Int):Double {
        val lat=(a[2*i]+a[2*j])/2
        return hypot((a[2*j+1]-a[2*i+1])*111_320*cos(Math.toRadians(lat)),(a[2*j]-a[2*i])*110_540)
    }

    @Test fun encodesGooglesDocumentedExample() {
        val points=doubleArrayOf(38.5,-120.2,40.7,-120.95,43.252,-126.453)
        assertEquals("_p~iF~ps|U_ulLnnqC_mqNvxq`@",RouteElevation.encode(points))
    }

    @Test fun samplesEvery20MetresAlongTheRoadAroundCorners() {
        // 310 m north, then 200 m east (a right-angle corner), near Thrissur.
        val lat0=10.52;val lon0=76.21;val kx=111_320*cos(Math.toRadians(lat0))
        val line=doubleArrayOf(lat0,lon0,lat0+310/110_540.0,lon0,lat0+310/110_540.0,lon0+200/kx)
        val points=RouteElevation.resample(line)
        assertEquals(27,points.size/2) // 0, 20, ..., 500 m along the road, then the end at 510 m
        for(i in 1 until points.size/2) {
            val step=metres(points,i-1,i)
            // 20 m along the road: 10 m to the corner and 10 m beyond is 14.1 m as the crow flies.
            val expected=when(i){16->14.142;26->10.0;else->20.0}
            assertEquals("step $i",expected,step,.05)
        }
        assertEquals(lat0,points[0],1e-12);assertEquals(lon0+200/kx,points[points.size-1],1e-7)
    }

    @Test fun stopsAt100Kilometres() {
        val line=doubleArrayOf(10.0,76.0,10.0+150_000/110_540.0,76.0)
        val points=RouteElevation.resample(line)
        assertEquals(5001,points.size/2)
        assertTrue(RouteElevation.resample(doubleArrayOf(10.0,76.0)).isEmpty())
    }
}
