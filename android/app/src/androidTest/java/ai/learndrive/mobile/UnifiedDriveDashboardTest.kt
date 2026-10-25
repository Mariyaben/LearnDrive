package ai.learndrive.mobile

import android.graphics.Rect
import android.text.Editable
import android.text.TextWatcher
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import ai.learndrive.mobile.ui.UnifiedDriveDashboard
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.math.roundToInt

/** Real Android widgets in a software-laid-out tree; no models, camera or network.
 * Window/IME and hardware touch dispatch are covered by the activity integration check. */
@RunWith(AndroidJUnit4::class)
class UnifiedDriveDashboardTest {
    @Test fun repeatedPresentationsPreserveDraftScrollAndPressedControlBounds() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        instrumentation.runOnMainSync {
            val context=instrumentation.targetContext
            fun dp(value:Int)=(value*context.resources.displayMetrics.density).roundToInt()
            val parent=FrameLayout(context)
            val dashboard=UnifiedDriveDashboard(context,View(context))
            parent.addView(dashboard,FrameLayout.LayoutParams(-1,-1))
            val scroll=ScrollView(context).apply {isSmoothScrollingEnabled=false}
            val column=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL}
            scroll.addView(column)
            dashboard.setupContentHost.addView(scroll,FrameLayout.LayoutParams(-1,-1))
            val draft=EditText(context).apply {isSingleLine=true;isFocusableInTouchMode=true}
            column.addView(draft,LinearLayout.LayoutParams(-1,dp(48)))
            val buttons=(0 until 24).map {index->Button(context).apply {
                text="Setup control $index"
                column.addView(this,LinearLayout.LayoutParams(-1,dp(48)))
            }}
            fun layoutTree() {
                repeat(3) {
                    parent.measure(View.MeasureSpec.makeMeasureSpec(dp(1000),View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(dp(600),View.MeasureSpec.EXACTLY))
                    parent.layout(0,0,dp(1000),dp(600))
                }
            }
            fun present(speed:Double=0.2,distance:Double=39.6) {
                dashboard.showSetup(true)
                dashboard.setMeasuredSpeed(speed,"GPS + IMU")
                dashboard.setMeasuredGear(null)
                dashboard.setDestination("Practice ground")
                dashboard.setManeuver("Turn right",distance)
                dashboard.setCoachPresentation("Set up while parked to start coaching","Coach status")
            }
            present();layoutTree()
            draft.setText("Kochi draft");draft.setSelection(5)
            assertTrue("The destination draft must accept focus",draft.requestFocus())
            layoutTree();scroll.scrollTo(0,dp(450))
            assertTrue("Fixture must actually be scrolled",scroll.scrollY>0)
            val pressed=buttons[12]
            pressed.isPressed=true
            var clicks=0;pressed.setOnClickListener {clicks++}
            fun pressedBounds()=Rect().also {bounds->
                pressed.getDrawingRect(bounds);parent.offsetDescendantRectToMyCoords(pressed,bounds)
            }
            val scrollY=scroll.scrollY;val bounds=pressedBounds()
            assertTrue("Pressed fixture must have positive visible bounds",bounds.width()>0&&bounds.height()>0)
            assertFalse("Initial layout must have settled",dashboard.isLayoutRequested)
            var textChanges=0
            val observer=object:TextWatcher {
                override fun beforeTextChanged(text:CharSequence?,start:Int,count:Int,after:Int)=Unit
                override fun onTextChanged(text:CharSequence?,start:Int,before:Int,count:Int){textChanges++}
                override fun afterTextChanged(text:Editable?)=Unit
            }
            fun observe(view:View) {
                if(view is TextView)view.addTextChangedListener(observer)
                if(view is ViewGroup)for(i in 0 until view.childCount)observe(view.getChildAt(i))
            }
            observe(dashboard)
            repeat(100) {index->
                // Raw values differ, but the displayed rounded values stay the same.
                present(if(index%2==0)0.1 else 0.4,if(index%2==0)39.6 else 40.2)
                assertFalse("Stable display updates must not schedule another layout",dashboard.isLayoutRequested)
                assertEquals("Scrolling must stay where the user left it",scrollY,scroll.scrollY)
                assertTrue("The draft must keep keyboard focus",draft.hasFocus())
                assertEquals("Kochi draft",draft.text.toString())
                assertEquals(5,draft.selectionStart)
                assertTrue("A held control must retain its pressed state",pressed.isPressed)
                assertEquals("Controls must not move under the user's finger",bounds,pressedBounds())
            }
            assertEquals("Unchanged HUD text must not emit text-change callbacks",0,textChanges)
            pressed.isPressed=false
            assertTrue("The selected setup control must still be clickable",pressed.performClick())
            assertEquals(1,clicks)

            dashboard.showSetup(false);layoutTree()
            assertEquals(View.GONE,dashboard.setupHost.visibility)
            repeat(10){dashboard.showSetup(false)}
            assertFalse("Repeated closed setup must also be layout-idempotent",dashboard.isLayoutRequested)
            dashboard.showSetup(true);layoutTree()
            assertEquals(View.VISIBLE,dashboard.setupHost.visibility)
            assertEquals("Closing setup must not discard the user's draft","Kochi draft",draft.text.toString())
            repeat(10){dashboard.showSetup(true)}
            assertFalse("Reopened setup must remain stable on later ticks",dashboard.isLayoutRequested)

            dashboard.setMeasuredSpeed(12.0,"GPS + IMU")
            dashboard.setManeuver("Turn left",250.0)
            dashboard.setDestination(null)
            assertEquals("Real destination changes must still render","Where to?",dashboard.destinationButton.text.toString())
            assertEquals("Turn left",dashboard.maneuverText.text.toString())
            assertEquals("~250 m",dashboard.maneuverDistance.text.toString())
            assertEquals("12",dashboard.findViewWithTag<TextView>("measured-speed").text.toString())
            assertTrue("Real presentation changes must still emit text updates",textChanges>0)
        }
    }
    @Test fun cueContextAndUnknownMeasurementsRemainExplicit() {
        InstrumentationRegistry.getInstrumentation().runOnMainSync {
            val context=InstrumentationRegistry.getInstrumentation().targetContext
            val dashboard=UnifiedDriveDashboard(context,View(context))
            dashboard.setCoachPresentation("Give the pedestrian space","Recent camera cue · 2s ago")
            assertEquals("Give the pedestrian space",dashboard.coachText.text.toString())
            assertEquals("Recent camera cue · 2s ago",dashboard.coachContext.text.toString())
            dashboard.setMeasuredGear(3) // A number without vehicle evidence must not appear measured.
            fun allText(view:View):List<String> = if(view is ViewGroup)
                (0 until view.childCount).flatMap {allText(view.getChildAt(it))}
                else if(view is TextView)listOf(view.text.toString()) else emptyList()
            assertTrue(allText(dashboard).contains("Gear —"))
            assertTrue(allText(dashboard).contains("Not measured"))
            assertFalse(allText(dashboard).contains("Gear 3"))
            dashboard.setManeuver("Turn right",10.0)
            assertEquals("Nearby",dashboard.maneuverDistance.text.toString())
            dashboard.setManeuver("Turn right",Double.NaN)
            assertEquals(View.GONE,dashboard.maneuverDistance.visibility)
        }
    }
}
