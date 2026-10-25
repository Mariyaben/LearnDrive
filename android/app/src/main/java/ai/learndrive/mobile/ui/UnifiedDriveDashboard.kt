package ai.learndrive.mobile.ui

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.text.TextUtils
import android.view.Gravity
import android.view.View
import android.widget.*
import kotlin.math.roundToInt

/** Persistent map/camera surface. Explicit parked interactions change pages. */
class UnifiedDriveDashboard(context:Context,roadView:View):FrameLayout(context) {
    private fun dp(v:Int)=CoachStyle.dp(context,v)
    private fun label(s:String,size:Float=14f,color:Int=CoachStyle.text)=CoachStyle.label(context,s,size,color)
    private fun action(s:String)=CoachStyle.button(context,s)
    private fun icon(kind:String,color:Int=CoachStyle.text)=ImageView(context).apply {
        setImageDrawable(CoachStyle.icon(context,kind,color));importantForAccessibility=IMPORTANT_FOR_ACCESSIBILITY_NO
    }
    val mapHost=FrameLayout(context).apply {tag="navigation-map-host"}
    val setupHost=FrameLayout(context).apply {tag="parked-setup-host";background=CoachStyle.panel(context,radius=0);elevation=dp(3).toFloat();visibility=GONE}
    val destinationHost=FrameLayout(context).apply {visibility=GONE}
    val setupContentHost=FrameLayout(context)
    val setupActionHost=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL}
    val prepareTab=action("Get ready").apply {tag="prepare-tab";isSelected=true;CoachStyle.styleTab(this)}
    val destinationTab=action("Destination").apply {tag="destination-tab";CoachStyle.styleTab(this)}
    val closeSetupButton=action("").apply {contentDescription="Close setup";tag="setup-close"
        setCompoundDrawablesRelativeWithIntrinsicBounds(CoachStyle.icon(context,"close"),null,null,null);setPadding(dp(12),0,dp(12),0)}
    val destinationButton=action("Where to?").apply {tag="destination-open";gravity=Gravity.START or Gravity.CENTER_VERTICAL;maxLines=1;ellipsize=TextUtils.TruncateAt.END;background=CoachStyle.panel(context,CoachStyle.raised,8,Color.TRANSPARENT)
        setCompoundDrawablesRelativeWithIntrinsicBounds(CoachStyle.icon(context,"search",CoachStyle.accent),null,null,null);compoundDrawablePadding=dp(10)}
    val settingsButton=action("Setup").apply {tag="drive-setup-open"}
    val sessionActionHost=FrameLayout(context)
    val sessionStatus=label("Parked",11f,CoachStyle.muted)
    val quickStartButton=CoachStyle.button(context,"Get ready",true).apply {tag="quick-start"}
    val coachContext=label("YOUR NEXT STEP",10f,CoachStyle.muted).apply {tag="coach-evidence-context";maxLines=1;ellipsize=TextUtils.TruncateAt.END;letterSpacing=.08f}
    val coachText=label("Set up while parked",18f).apply {tag="coach-instruction";maxLines=2;ellipsize=TextUtils.TruncateAt.END;gravity=Gravity.CENTER_VERTICAL
        typeface=Typeface.create("sans-serif-medium",Typeface.NORMAL);accessibilityLiveRegion=ACCESSIBILITY_LIVE_REGION_POLITE}
    val maneuverText=label("Free practice",15f).apply {tag="next-maneuver";maxLines=2;ellipsize=TextUtils.TruncateAt.END;typeface=Typeface.create("sans-serif-medium",0)}
    val maneuverDistance=label("",18f,CoachStyle.accent).apply {visibility=GONE;maxLines=1}
    val maneuverProvider=label("",11f,CoachStyle.muted).apply {maxLines=1}
    val navigationStatus=label("Add a destination, or practise without a route",11f,CoachStyle.muted).apply {maxLines=1;ellipsize=TextUtils.TruncateAt.END}
    val roadStatus=label("Getting the camera ready",11f,Color.WHITE).apply {maxLines=2;ellipsize=TextUtils.TruncateAt.END;setPadding(dp(12),dp(7),dp(12),dp(7));setBackgroundColor(Color.argb(218,20,22,25))}
    val detailsText=label("",12f,CoachStyle.muted).apply {visibility=GONE}
    private val speedValue=label("—",28f).apply {tag="measured-speed";typeface=Typeface.create("sans-serif-medium",0)}
    private val speedSource=label("GPS unavailable",9f,CoachStyle.muted).apply {maxLines=1;ellipsize=TextUtils.TruncateAt.END}
    private val gearValue=label("Gear —",13f)
    private val gearSource=label("Not measured",10f,CoachStyle.muted)
    private val gearTile=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER;visibility=GONE;addView(gearValue);addView(gearSource)}
    private val speedTile=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER;setPadding(dp(8),0,dp(8),0)
        addView(LinearLayout(context).apply {gravity=Gravity.BOTTOM;addView(speedValue);addView(label("km/h",10f,CoachStyle.muted),LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(4);bottomMargin=dp(4)})});addView(speedSource)}
    private val header=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL}
    private val body=LinearLayout(context)
    private val coachStrip=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;setBackgroundColor(CoachStyle.surface)}
    private val mapColumn=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;background=CoachStyle.panel(context);clipToOutline=true}
    private val roadHost=FrameLayout(context).apply {tag="road-camera-host";background=CoachStyle.panel(context);clipToOutline=true}
    private val sheetHeading=label("Before you drive",20f).apply {typeface=Typeface.create("sans-serif-medium",0)}
    private val sheetHeader=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL}
    private val tabs=LinearLayout(context)
    private val directionIcon=icon("location",CoachStyle.accent)
    private var directionKind="location"
    private var destinationPage=false
    private var keyboardVisible=false
    private var running=false
    private var layoutKey=""
    val readiness=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL}
    private data class Check(val detail:TextView,val symbol:ImageView,var key:String="")
    private val checks=mutableListOf<Check>()
    init {
        setBackgroundColor(CoachStyle.surface)
        val content=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL};addView(content,LayoutParams(-1,-1))
        header.addView(icon("brand"),LinearLayout.LayoutParams(dp(24),dp(28)))
        header.addView(LinearLayout(context).apply {orientation=LinearLayout.VERTICAL
            addView(label("LearnDrive",18f).apply {typeface=Typeface.create("sans-serif-medium",0)})
            addView(sessionStatus,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(3)})},LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(8);marginEnd=dp(14)})
        header.addView(destinationButton,LinearLayout.LayoutParams(0,dp(48),1f));header.addView(speedTile,LinearLayout.LayoutParams(dp(92),-1));header.addView(gearTile,LinearLayout.LayoutParams(dp(68),-1))
        header.addView(sessionActionHost,LinearLayout.LayoutParams(-2,dp(48)).apply {marginStart=dp(6)})
        header.addView(settingsButton,LinearLayout.LayoutParams(-2,dp(48)).apply {marginStart=dp(6)})
        content.addView(header,LinearLayout.LayoutParams(-1,dp(54)))
        content.addView(body,LinearLayout.LayoutParams(-1,0,1f).apply {topMargin=dp(8);bottomMargin=dp(8)})
        coachStrip.setPadding(dp(2),dp(10),dp(2),dp(2));coachStrip.addView(icon("coach"),LinearLayout.LayoutParams(dp(28),dp(28)).apply {marginEnd=dp(14)})
        coachStrip.addView(LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;addView(coachContext,LinearLayout.LayoutParams(-1,-2));addView(coachText,LinearLayout.LayoutParams(-1,0,1f).apply {topMargin=dp(4)})},LinearLayout.LayoutParams(0,-1,1f))
        coachStrip.addView(quickStartButton,LinearLayout.LayoutParams(-2,dp(48)).apply {marginStart=dp(12)});content.addView(coachStrip,LinearLayout.LayoutParams(-1,dp(70)))
        mapColumn.addView(mapHost,LinearLayout.LayoutParams(-1,0,1f))
        val routeCard=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;setPadding(dp(12),dp(8),dp(12),dp(8))}
        routeCard.addView(directionIcon,LinearLayout.LayoutParams(dp(26),dp(26)).apply {marginEnd=dp(10)})
        routeCard.addView(LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;addView(maneuverText);addView(navigationStatus,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(4)})},LinearLayout.LayoutParams(0,-2,1f))
        routeCard.addView(LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;gravity=Gravity.END;addView(maneuverDistance);addView(maneuverProvider)},LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(8)})
        mapColumn.addView(routeCard,LinearLayout.LayoutParams(-1,dp(58)))
        roadHost.addView(roadView,LayoutParams(-1,-1))
        roadHost.addView(label("ROAD CAMERA",10f,Color.WHITE).apply {letterSpacing=.1f;setPadding(dp(10),dp(6),dp(10),dp(6));background=CoachStyle.panel(context,Color.argb(205,20,22,25),6,Color.TRANSPARENT)},LayoutParams(-2,-2,Gravity.TOP or Gravity.START).apply {topMargin=dp(8);marginStart=dp(8)})
        roadHost.addView(roadStatus,LayoutParams(-1,-2,Gravity.BOTTOM));body.addView(mapColumn);body.addView(roadHost)
        val sheet=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;setPadding(dp(16),dp(12),dp(16),dp(12))}
        sheetHeader.addView(sheetHeading,LinearLayout.LayoutParams(0,-2,1f));sheetHeader.addView(closeSetupButton,LinearLayout.LayoutParams(dp(48),dp(48)));sheet.addView(sheetHeader,LinearLayout.LayoutParams(-1,dp(48)))
        tabs.addView(prepareTab,LinearLayout.LayoutParams(0,dp(48),1f));tabs.addView(destinationTab,LinearLayout.LayoutParams(0,dp(48),1f))
        sheet.addView(tabs,LinearLayout.LayoutParams(-1,dp(48)).apply {bottomMargin=dp(8)})
        val pages=FrameLayout(context).apply {addView(setupContentHost,LayoutParams(-1,-1));addView(destinationHost,LayoutParams(-1,-1))}
        sheet.addView(pages,LinearLayout.LayoutParams(-1,0,1f));sheet.addView(setupActionHost,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(10)})
        setupHost.addView(sheet,LayoutParams(-1,-1));addView(setupHost,LayoutParams(dp(440),-1,Gravity.END))
        for((kind,title) in listOf("camera" to "Camera","voice" to "Voice","location" to "GPS · optional")) {
            val symbol=icon(kind,CoachStyle.muted);val heading=label(title,12f);val detail=label("Checking…",11f,CoachStyle.muted).apply {maxLines=1;ellipsize=TextUtils.TruncateAt.END}
            readiness.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;setPadding(0,dp(3),0,dp(3))
                addView(symbol,LinearLayout.LayoutParams(dp(22),dp(22)).apply {marginEnd=dp(12)})
                addView(LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;addView(heading);addView(detail,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(2)})},LinearLayout.LayoutParams(0,-2,1f))},LinearLayout.LayoutParams(-1,dp(38)))
            checks.add(Check(detail,symbol))
        }
    }
    fun attachMap(view:View){if(mapHost.childCount==1&&mapHost.getChildAt(0)===view)return;mapHost.removeAllViews();mapHost.addView(view,LayoutParams(-1,-1))}
    fun showSetup(show:Boolean) {
        if((setupHost.visibility==VISIBLE)==show)return
        setupHost.animate().cancel();setupHost.visibility=if(show)VISIBLE else GONE
        if(show&&getChildAt(childCount-1)!==setupHost)setupHost.bringToFront()
        if(show&&ValueAnimator.areAnimatorsEnabled()&&isAttachedToWindow){setupHost.alpha=0f;setupHost.translationX=dp(12).toFloat();setupHost.animate().alpha(1f).translationX(0f).setDuration(180).start()}
        else {setupHost.alpha=1f;setupHost.translationX=0f}
        layoutKey="";updateChrome(width,height)
    }
    fun showDestinationPage(show:Boolean) {
        if(destinationPage==show)return
        destinationPage=show;destinationHost.visibility=if(show)VISIBLE else GONE;setupContentHost.visibility=if(show)GONE else VISIBLE
        setupActionHost.visibility=if(show||keyboardVisible)GONE else VISIBLE;prepareTab.isSelected=!show;destinationTab.isSelected=show
        sheetHeading.setTextIfChanged(if(show)"Choose a destination" else "Before you drive");setupHost.accessibilityPaneTitle=if(show)"Choose destination" else "Drive setup"
    }
    fun setKeyboardVisible(visible:Boolean){if(keyboardVisible==visible)return;keyboardVisible=visible;layoutKey="";updateChrome(width,height)}
    fun setSessionRunning(value:Boolean){if(running==value)return;running=value;quickStartButton.visibility=if(value)GONE else VISIBLE}
    fun setReadiness(index:Int,detail:String,ready:Boolean) {
        val check=checks[index];val key="$detail:$ready";if(check.key==key)return;check.key=key;check.detail.setTextIfChanged(detail)
        check.symbol.setImageDrawable(CoachStyle.icon(context,if(ready)"check" else listOf("camera","voice","location")[index],if(ready)CoachStyle.accent else CoachStyle.muted))
    }
    fun setMeasuredSpeed(kph:Double?,source:String="GPS") {
        val valid=kph?.takeIf {it.isFinite()&&it in 0.0..300.0};speedValue.setTextIfChanged(valid?.roundToInt()?.toString()?:"—");speedSource.setTextIfChanged(if(valid==null)"Speed unavailable" else source)
    }
    fun setMeasuredGear(gear:Int?,evidence:String?=null) {
        val valid=gear?.takeIf {it in 1..12&&!evidence.isNullOrBlank()};gearValue.setTextIfChanged(valid?.let {"Gear $it"}?:"Gear —");gearSource.setTextIfChanged(if(valid==null)"Not measured" else evidence.orEmpty())
        val visibility=if(valid==null)GONE else VISIBLE;if(gearTile.visibility!=visibility)gearTile.visibility=visibility
    }
    fun setDestination(label:String?){destinationButton.setTextIfChanged(label?.takeIf {it.isNotBlank()}?:"Where to?")}
    fun setCoachPresentation(instruction:String,context:String){coachText.setTextIfChanged(instruction);coachContext.setTextIfChanged(context)}
    fun setManeuver(instruction:String?,distanceMetres:Double?=null) {
        maneuverText.setTextIfChanged(instruction?.takeIf {it.isNotBlank()}?:"Free practice")
        val distance=distanceMetres?.takeIf {it.isFinite()&&it>=0};val visible=if(distance==null)GONE else VISIBLE;if(maneuverDistance.visibility!=visible)maneuverDistance.visibility=visible
        maneuverDistance.setTextIfChanged(when {distance==null->"";distance>=1000->"~%.1f km".format(distance/1000);distance>=20->"~${(distance/10).roundToInt()*10} m";else->"Nearby"})
        val kind=when {instruction==null->"location";instruction.contains("left",true)->"left";instruction.contains("right",true)->"right";instruction.contains("straight",true)->"straight";else->"location"}
        if(kind!=directionKind){directionKind=kind;directionIcon.setImageDrawable(CoachStyle.icon(context,kind,CoachStyle.accent))}
    }
    override fun onMeasure(widthMeasureSpec:Int,heightMeasureSpec:Int){updateChrome(MeasureSpec.getSize(widthMeasureSpec),MeasureSpec.getSize(heightMeasureSpec));super.onMeasure(widthMeasureSpec,heightMeasureSpec)}
    private fun updateChrome(w:Int,h:Int) {
        if(w<=0||h<=0)return
        val availableW=(w-paddingLeft-paddingRight).coerceAtLeast(0);val availableH=(h-paddingTop-paddingBottom).coerceAtLeast(0);val open=setupHost.visibility==VISIBLE;val landscape=w>=h
        val key="$availableW:$availableH:$open:$keyboardVisible:$landscape";if(key==layoutKey)return;layoutKey=key
        val sheetWidth=if(landscape)minOf(dp(480),(availableW*.52f).roundToInt()) else availableW;val leftWidth=if(open&&landscape)(availableW-sheetWidth-dp(10)).coerceAtLeast(0) else -1
        (header.layoutParams as? LinearLayout.LayoutParams)?.let {it.width=leftWidth;header.layoutParams=it};(coachStrip.layoutParams as? LinearLayout.LayoutParams)?.let {it.width=leftWidth;coachStrip.layoutParams=it}
        header.visibility=if(keyboardVisible)GONE else VISIBLE;coachStrip.visibility=if(keyboardVisible)GONE else VISIBLE;speedTile.visibility=if(open)GONE else VISIBLE;settingsButton.visibility=if(open)GONE else VISIBLE
        quickStartButton.visibility=if(running||open)GONE else VISIBLE;sheetHeader.visibility=if(keyboardVisible)GONE else VISIBLE;tabs.visibility=if(keyboardVisible)GONE else VISIBLE
        setupActionHost.visibility=if(destinationPage||keyboardVisible)GONE else VISIBLE
        setupHost.layoutParams=(setupHost.layoutParams as LayoutParams).apply {width=sheetWidth;height=-1;gravity=Gravity.END}
        body.orientation=if(landscape)LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
        mapColumn.layoutParams=if(landscape)LinearLayout.LayoutParams(0,-1,.64f) else LinearLayout.LayoutParams(-1,0,.56f)
        roadHost.layoutParams=if(landscape)LinearLayout.LayoutParams(0,-1,.36f).apply {marginStart=dp(10)} else LinearLayout.LayoutParams(-1,0,.44f).apply {topMargin=dp(10)}
    }
    private fun TextView.setTextIfChanged(value:CharSequence){if(!TextUtils.equals(text,value))text=value}
}
