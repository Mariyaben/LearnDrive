package ai.learndrive.mobile

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.PointF
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class DetectionOverlayTest {
    private class ObservedOverlay(context:Context):DetectionOverlay(context) {
        var invalidations=0
        override fun invalidate() {invalidations++;super.invalidate()}
    }

    /** Software bitmap rendering only; no Activity, camera, model initialization or capture. */
    @Test fun newFramesRedrawAndExpiredResultsClearWithoutRepeatedInvalidations() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        instrumentation.runOnMainSync {
            val overlay=ObservedOverlay(instrumentation.targetContext)
            val size=View.MeasureSpec.makeMeasureSpec(SIZE,View.MeasureSpec.EXACTLY)
            overlay.measure(size,size);overlay.layout(0,0,SIZE,SIZE)
            overlay.invalidations=0
            val first=VisionResult(10.0,Double.POSITIVE_INFINITY,0.0,false,100.0,
                listOf(Detection(.125f,.5f,.125f,.125f,.9f,2)),SIZE,SIZE,
                RoadShape(true,0,listOf(PointF(.25f,.875f),PointF(.375f,.875f))),completedAt=10.1)

            overlay.result=first
            assertEquals(1,overlay.invalidations)
            val original=render(overlay)
            assertTrue("A detection box must be drawn",Color.alpha(original[144*SIZE+32])>0)
            assertTrue("The road path must be drawn",Color.alpha(original[224*SIZE+80])>0)

            repeat(5) {overlay.result=first}
            assertEquals("Repeated display ticks must not request another redraw",1,overlay.invalidations)

            // A separately published value must still redraw, even when its contents are equal.
            overlay.result=first.copy()
            assertEquals(2,overlay.invalidations)
            assertArrayEquals(original,render(overlay))

            val replacement=first.copy(time=10.2,completedAt=10.3,road=null,
                detections=listOf(Detection(.625f,.5f,.125f,.125f,.9f,2)))
            overlay.result=replacement
            assertEquals(3,overlay.invalidations)
            val changed=render(overlay)
            assertEquals("Old box geometry must disappear",0,changed[144*SIZE+32])
            assertTrue("The replacement box must be drawn",Color.alpha(changed[144*SIZE+160])>0)
            assertEquals("An absent road estimate must erase the old path",0,changed[224*SIZE+80])

            // MainActivity uses this same freshness gate before assigning the overlay result.
            assertFalse(CameraVisionHealth.canDisplay(13.4,replacement.time,replacement.completedAt,0,null))
            overlay.result=null
            assertEquals("Expired/source-unavailable results must request a clearing redraw",4,overlay.invalidations)
            assertTrue("Clearing must leave no old boxes, labels or road path",render(overlay).all {it==0})
            repeat(5) {overlay.result=null}
            assertEquals("Repeated unavailable ticks need no further clearing redraw",4,overlay.invalidations)
        }
    }

    private fun render(view:View):IntArray {
        val bitmap=Bitmap.createBitmap(SIZE,SIZE,Bitmap.Config.ARGB_8888)
        return try {
            view.draw(Canvas(bitmap))
            IntArray(SIZE*SIZE).also {bitmap.getPixels(it,0,SIZE,0,0,SIZE,SIZE)}
        }finally {bitmap.recycle()}
    }

    private companion object {const val SIZE=256}
}
