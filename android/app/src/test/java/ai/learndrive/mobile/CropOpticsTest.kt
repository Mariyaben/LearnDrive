package ai.learndrive.mobile

import org.junit.Assert.assertEquals
import org.junit.Test

/** The detector's camera geometry through the preview crop (0.8.1 fills a wide panel with the
 * bottom of the 4:3 frame; the coach must know where a level horizon really is in that crop). */
class CropOpticsTest {
    // A 4000x3000 sensor with a 23 mm-equivalent lens: ~0.665 sensor widths per unit tangent.
    private val sensor=doubleArrayOf(2660.0,2660.0,2000.0,1500.0,4000.0,3000.0)

    @Test fun uncroppedFrameKeepsTheCentreMidImage() {
        val o=cropOptics(1280,960,0,0,1280,960,0,sensor)
        assertEquals(.665,o[0],.001);assertEquals(.887,o[1],.001);assertEquals(.5,o[2],1e-9);assertEquals(.5,o[3],1e-9)
    }

    @Test fun fillEndCropPutsTheLevelHorizonHighInTheImage() {
        // A 1065x575 panel shows the bottom 692 rows of a 1280x960 frame.
        val o=cropOptics(1280,960,0,268,1280,692,0,sensor)
        assertEquals(.665,o[0],.001)
        assertEquals(1.230,o[1],.002)      // a radian spans more of the cropped height
        assertEquals(.306,o[3],.001)       // the optical centre is ~31 % down, not mid-image
    }

    @Test fun streamSizeDoesNotChangeTheFractions() {
        val small=cropOptics(640,480,0,134,640,346,0,sensor);val large=cropOptics(1280,960,0,268,1280,692,0,sensor)
        for(i in 0..3)assertEquals(large[i],small[i],.003)
    }

    @Test fun portraitRotationSwapsTheAxes() {
        val o=cropOptics(1280,960,0,0,1280,960,90,sensor)
        assertEquals(.887,o[0],.001);assertEquals(.665,o[1],.001);assertEquals(.5,o[2],1e-9);assertEquals(.5,o[3],1e-9)
    }

    @Test fun withoutCalibrationAssumesTheFourByThreeDefaults() {
        val o=cropOptics(640,480,0,0,640,480,0,null)
        assertEquals(.72,o[0],1e-9);assertEquals(.96,o[1],1e-9);assertEquals(.5,o[2],1e-9);assertEquals(.5,o[3],1e-9)
    }
}
