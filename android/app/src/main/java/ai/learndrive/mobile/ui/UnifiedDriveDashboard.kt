package ai.learndrive.mobile.ui

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Color
import android.text.Editable
import android.text.TextUtils
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.widget.*
import kotlin.math.roundToInt

/**
 * The drive screen. Left: the next turn (Google's turn-by-turn feed, or the map when parked) and the
 * coach's instruction. Right: destination, speed, Setup and End over the road camera. A slim strip
 * on top shows the time, the session and the road. Setup slides in over the camera while parked.
 */
class UnifiedDriveDashboard(context:Context,roadView:View):FrameLayout(context) {
    private fun dp(v:Int)=CoachStyle.dp(context,v)
    private fun label(s:String,size:Float=14f,color:Int=CoachStyle.text)=CoachStyle.label(context,s,size,color)
    private fun action(s:String)=CoachStyle.button(context,s)
    private fun icon(kind:String,color:Int=CoachStyle.text,weight:Float=2.1f)=ImageView(context).apply {
        setImageDrawable(CoachStyle.icon(context,kind,color,weight));importantForAccessibility=IMPORTANT_FOR_ACCESSIBILITY_NO
    }
    private fun row(vararg views:View)=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;views.forEach {addView(it)}}

    // ---- the strip: time, the session, the road
    private val clock=TextClock(context).apply {format24Hour="HH:mm";format12Hour="h:mm";textSize=13f;setTextColor(CoachStyle.text);typeface=CoachStyle.bold;includeFontPadding=false}
    private val brand=label("LEARNDRIVE",12f,CoachStyle.amber).apply {typeface=CoachStyle.bold;letterSpacing=.1f}
    val sessionStatus=label("PARKED",12f,CoachStyle.lighten(CoachStyle.amber,.35f)).apply {typeface=CoachStyle.bold;letterSpacing=.1f;isAllCaps=true;maxLines=1}
    private val roadName=label("",12f,CoachStyle.muted).apply {maxLines=1;ellipsize=TextUtils.TruncateAt.END;typeface=CoachStyle.medium}
    private val battery=label("",11f,CoachStyle.go).apply {typeface=CoachStyle.bold;visibility=GONE;setPadding(dp(8),dp(2),dp(8),dp(2))
        background=CoachStyle.panel(context,Color.rgb(12,52,40),7,Color.TRANSPARENT)}
    private val strip=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;setPadding(dp(4),0,dp(4),0)}

    // ---- the top bar: where to, how fast, Setup, End
    private val destinationName=label("Where to?",15f).apply {typeface=CoachStyle.bold;maxLines=1;ellipsize=TextUtils.TruncateAt.END}
    private val tripDistance=label("",12f,Color.rgb(201,211,223)).apply {typeface=CoachStyle.medium;maxLines=1}
    private val tripTime=label("",12f,CoachStyle.go).apply {typeface=CoachStyle.bold;maxLines=1}
    private val tripRow=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;visibility=GONE
        addView(tripDistance);addView(label("·",12f,CoachStyle.muted),LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(6);marginEnd=dp(6)});addView(tripTime)}
    private val lockIcon=icon("lock",CoachStyle.muted).apply {visibility=GONE}
    /** The destination pill. Disabled (locked) while driving: the destination is changed parked. */
    val destinationButton:LinearLayout=object:LinearLayout(context) {
        override fun setEnabled(enabled:Boolean){super.setEnabled(enabled);lockIcon.visibility=if(enabled||destinationName.text=="Where to?")GONE else VISIBLE}
    }.apply {tag="destination-open";gravity=Gravity.CENTER_VERTICAL;setPadding(dp(12),0,dp(10),0);isClickable=true;isFocusable=true
        background=android.graphics.drawable.RippleDrawable(android.content.res.ColorStateList.valueOf(Color.argb(40,56,189,248)),CoachStyle.panel(context,CoachStyle.surface,16,CoachStyle.line),null)
        contentDescription="Destination"
        addView(icon("pin",CoachStyle.go),LinearLayout.LayoutParams(dp(20),dp(20)).apply {marginEnd=dp(8)})
        addView(LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER_VERTICAL
            addView(destinationName,LinearLayout.LayoutParams(-1,-2));addView(tripRow,LinearLayout.LayoutParams(-2,-2).apply {topMargin=dp(3)})},LinearLayout.LayoutParams(0,-2,1f))
        addView(lockIcon,LinearLayout.LayoutParams(dp(15),dp(15)).apply {marginStart=dp(8)})}
    private val speedValue=label("—",28f).apply {tag="measured-speed";typeface=CoachStyle.display;fontFeatureSettings="tnum"}
    private val speedLimit=label("",13f,Color.rgb(20,20,20)).apply {typeface=CoachStyle.bold;gravity=Gravity.CENTER;visibility=GONE
        background=android.graphics.drawable.GradientDrawable().apply {shape=android.graphics.drawable.GradientDrawable.OVAL;setColor(Color.WHITE);setStroke(dp(3),Color.rgb(214,40,40))}}
    private val gpsDot=DotIcon(context,CoachStyle.muted,7)
    private val speedSource=label("No speed",11f,CoachStyle.muted).apply {typeface=CoachStyle.medium;maxLines=1
        setCompoundDrawablesRelativeWithIntrinsicBounds(gpsDot,null,null,null);compoundDrawablePadding=dp(5)}
    private val speedTile=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;setPadding(dp(14),0,dp(12),0)
        background=CoachStyle.panel(context,CoachStyle.surface,16,CoachStyle.line)
        addView(speedValue);addView(label("km/h",11f,CoachStyle.muted),LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(4);topMargin=dp(8)})
        addView(speedLimit,LinearLayout.LayoutParams(dp(30),dp(30)).apply {marginStart=dp(10)})
        addView(speedSource,LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(10)})}
    val settingsButton=action("Setup").apply {tag="drive-setup-open";compoundDrawablePadding=dp(8)
        setCompoundDrawablesRelativeWithIntrinsicBounds(CoachStyle.icon(context,"settings",CoachStyle.text),null,null,null)}
    val sessionActionHost=FrameLayout(context)
    private val topBar=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL}

    // ---- the turn card (Google's next step), or the map while parked
    val mapHost=FrameLayout(context).apply {tag="navigation-map-host"}
    private var turnIconKind="straight"
    private val turnIcon=icon("straight",Color.WHITE,2.6f)
    private val turnTile=FrameLayout(context).apply {background=CoachStyle.panel(context,Color.argb(110,0,40,28),14,Color.argb(70,255,255,255))
        addView(turnIcon,LayoutParams(dp(34),dp(34),Gravity.CENTER))}
    val maneuverDistance=label("",30f,Color.WHITE).apply {typeface=CoachStyle.display;maxLines=1;fontFeatureSettings="tnum";visibility=GONE}
    val maneuverText=label("Free practice",17f,Color.WHITE).apply {tag="next-maneuver";typeface=CoachStyle.bold;maxLines=2;ellipsize=TextUtils.TruncateAt.END
        setAutoSizeTextTypeUniformWithConfiguration(13,17,1,android.util.TypedValue.COMPLEX_UNIT_SP)}
    private val shield=label("",13f,Color.rgb(25,22,8)).apply {typeface=CoachStyle.bold;visibility=GONE;setPadding(dp(9),dp(4),dp(9),dp(4))
        background=CoachStyle.panel(context,Color.rgb(250,196,38),7,Color.TRANSPARENT)}
    private var thenIconKind="straight"
    private val thenIcon=icon("straight",Color.WHITE,2.4f)
    private val thenText=label("",13f,Color.WHITE).apply {typeface=CoachStyle.bold;maxLines=1;ellipsize=TextUtils.TruncateAt.END}
    private val thenChip=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;setPadding(dp(10),dp(5),dp(12),dp(5));visibility=GONE
        background=CoachStyle.panel(context,Color.argb(120,0,45,32),10,Color.TRANSPARENT)
        addView(label("THEN",11f,Color.rgb(134,239,172)).apply {typeface=CoachStyle.bold;letterSpacing=.08f})
        addView(thenIcon,LinearLayout.LayoutParams(dp(15),dp(15)).apply {marginStart=dp(8);marginEnd=dp(6)})
        addView(thenText,LinearLayout.LayoutParams(-2,-2))}
    /** Whose data the card shows ("Google Maps"): their terms require it in the same card. */
    val maneuverProvider=label("",12f,Color.WHITE).apply {maxLines=1;contentDescription="Google Maps"}
    val navigationStatus=label("Add a destination, or practise without a route",12f,Color.rgb(201,211,223)).apply {maxLines=1;ellipsize=TextUtils.TruncateAt.END;visibility=GONE}
    private val turnDivider=View(context).apply {setBackgroundColor(Color.argb(60,255,255,255))}
    private val turnCard=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;setPadding(dp(14),dp(12),dp(14),dp(10))}
    private val routePanel=FrameLayout(context).apply {background=CoachStyle.panel(context,CoachStyle.surface,20,CoachStyle.line);clipToOutline=true}
    private var guidingStyle:Boolean?=null

    // ---- the coach card
    private val coachDot=DotIcon(context,CoachStyle.blue,8)
    val coachContext=label("YOUR NEXT STEP",12f,CoachStyle.blue).apply {tag="coach-evidence-context";typeface=CoachStyle.bold;letterSpacing=.1f;maxLines=1;ellipsize=TextUtils.TruncateAt.END
        setCompoundDrawablesRelativeWithIntrinsicBounds(coachDot,null,null,null);compoundDrawablePadding=dp(8)}
    private val gearChip=label("",12f).apply {typeface=CoachStyle.bold;visibility=GONE;setPadding(dp(10),dp(5),dp(10),dp(5));background=CoachStyle.panel(context,CoachStyle.surface,9,CoachStyle.line)}
    private var coachIconKind="coach"
    private val coachIcon=icon("coach",CoachStyle.ink,2.5f)
    private val coachTile=FrameLayout(context).apply {addView(coachIcon,LayoutParams(dp(36),dp(36),Gravity.CENTER))}
    val coachText=label("Set up while parked",30f).apply {tag="coach-instruction";maxLines=2;ellipsize=TextUtils.TruncateAt.END;typeface=CoachStyle.display
        accessibilityLiveRegion=ACCESSIBILITY_LIVE_REGION_POLITE;setAutoSizeTextTypeUniformWithConfiguration(16,30,1,android.util.TypedValue.COMPLEX_UNIT_SP)}
    private val coachSubtitle=label("",15f,CoachStyle.cream).apply {typeface=CoachStyle.medium;maxLines=2;ellipsize=TextUtils.TruncateAt.END}
    private val chipRow=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL}
    private val chipDivider=View(context).apply {setBackgroundColor(CoachStyle.line)}
    val quickStartButton=CoachStyle.button(context,"Get ready",true).apply {tag="quick-start"}
    private val coachCard=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;setPadding(dp(16),dp(14),dp(16),dp(12))}
    private var coachKey=""
    /** Kept for the lab panel; never shown while driving. */
    val detailsText=label("",12f,CoachStyle.muted).apply {visibility=GONE}

    // ---- the road camera
    val roadStatus=CoachStyle.chip(context,"").apply {visibility=GONE;maxLines=1;ellipsize=TextUtils.TruncateAt.END
        background=CoachStyle.panel(context,Color.argb(215,9,13,19),9,CoachStyle.line)}
    private val aimDot=DotIcon(context,CoachStyle.amber,8)
    private val aimChip=label("",12f,Color.rgb(255,226,160)).apply {typeface=CoachStyle.bold;visibility=GONE;setPadding(dp(12),dp(7),dp(14),dp(7))
        background=CoachStyle.panel(context,Color.argb(220,20,16,6),12,CoachStyle.amber);setCompoundDrawablesRelativeWithIntrinsicBounds(aimDot,null,null,null);compoundDrawablePadding=dp(8)}
    private val roadHost=FrameLayout(context).apply {tag="road-camera-host";background=CoachStyle.panel(context,CoachStyle.surface,20,CoachStyle.line);clipToOutline=true}

    // ---- layout
    private val content=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL}
    private val body=LinearLayout(context)
    private val leftColumn=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL}
    private val rightColumn=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL}
    private var mapMode=true
    private var running=false
    private var layoutKey=""

    // ---- the setup sheet (parked)
    // Same corner radius as the camera panel it covers, so no sliver of the picture shows at its corners.
    val setupHost=FrameLayout(context).apply {tag="parked-setup-host";background=CoachStyle.panel(context,CoachStyle.surface,20,CoachStyle.line);elevation=dp(8).toFloat();visibility=GONE}
    val destinationHost=FrameLayout(context).apply {visibility=GONE}
    val setupContentHost=FrameLayout(context)
    val setupActionHost=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL}
    val prepareTab=action("Get ready").apply {tag="prepare-tab";isSelected=true;CoachStyle.styleTab(this)}
    val destinationTab=action("Destination").apply {tag="destination-tab";CoachStyle.styleTab(this)}
    val closeSetupButton=action("").apply {contentDescription="Close setup";tag="setup-close"
        setCompoundDrawablesRelativeWithIntrinsicBounds(CoachStyle.icon(context,"close"),null,null,null);setPadding(dp(12),0,dp(12),0)}
    private val sheetHeading=label("Before you drive",24f).apply {typeface=CoachStyle.display}
    private val sheetHeader=LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL}
    private val tabs=LinearLayout(context).apply {background=CoachStyle.panel(context,CoachStyle.ink,14,CoachStyle.line);setPadding(dp(4),dp(4),dp(4),dp(4))}
    private var destinationPage=false
    private var keyboardVisible=false
    val readiness=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL}
    private data class Check(val detail:TextView,val symbol:ImageView,var key:String="")
    private val checks=mutableListOf<Check>()

    init {
        setBackgroundColor(CoachStyle.ink)
        addView(content,LayoutParams(-1,-1))
        // Strip
        strip.addView(clock);strip.addView(brand,LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(12)})
        strip.addView(sessionStatus,LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(8)})
        strip.addView(View(context),LinearLayout.LayoutParams(0,1,1f))
        strip.addView(roadName,LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(12)})
        strip.addView(battery,LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(10)})
        content.addView(strip,LinearLayout.LayoutParams(-1,dp(22)))
        content.addView(body,LinearLayout.LayoutParams(-1,0,1f).apply {topMargin=dp(6)})
        // Turn card
        val distanceColumn=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;addView(maneuverDistance);addView(maneuverText,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(2)})
            addView(navigationStatus,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(4)})}
        turnCard.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL
            addView(turnTile,LinearLayout.LayoutParams(dp(58),dp(58)).apply {marginEnd=dp(14)})
            addView(distanceColumn,LinearLayout.LayoutParams(0,-2,1f))
            addView(shield,LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(8);gravity=Gravity.TOP})},LinearLayout.LayoutParams(-1,0,1f))
        turnCard.addView(turnDivider,LinearLayout.LayoutParams(-1,dp(1)).apply {topMargin=dp(8);bottomMargin=dp(8)})
        turnCard.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL
            addView(thenChip,LinearLayout.LayoutParams(0,-2,1f).apply {marginEnd=dp(8)})
            addView(maneuverProvider,LinearLayout.LayoutParams(-2,-2))},LinearLayout.LayoutParams(-1,-2))
        routePanel.addView(mapHost,LayoutParams(-1,-1));routePanel.addView(turnCard,LayoutParams(-1,-1))
        // Coach card
        coachCard.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL
            addView(coachContext,LinearLayout.LayoutParams(0,-2,1f));addView(gearChip,LinearLayout.LayoutParams(-2,-2).apply {marginStart=dp(8)})},LinearLayout.LayoutParams(-1,-2))
        coachCard.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL
            addView(coachTile,LinearLayout.LayoutParams(dp(64),dp(64)).apply {marginEnd=dp(16)})
            addView(LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER_VERTICAL
                addView(coachText,LinearLayout.LayoutParams(-1,-2));addView(coachSubtitle,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(6)})},LinearLayout.LayoutParams(0,-2,1f))},
            LinearLayout.LayoutParams(-1,0,1f))
        coachCard.addView(chipDivider,LinearLayout.LayoutParams(-1,dp(1)).apply {bottomMargin=dp(10)})
        coachCard.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL
            addView(HorizontalScrollView(context).apply {isHorizontalScrollBarEnabled=false;addView(chipRow)},LinearLayout.LayoutParams(0,-2,1f))
            addView(quickStartButton,LinearLayout.LayoutParams(-2,dp(46)).apply {marginStart=dp(10)})},LinearLayout.LayoutParams(-1,-2))
        leftColumn.addView(routePanel,LinearLayout.LayoutParams(-1,0,.36f))
        leftColumn.addView(coachCard,LinearLayout.LayoutParams(-1,0,.64f).apply {topMargin=dp(10)})
        // Top bar and camera
        topBar.addView(destinationButton,LinearLayout.LayoutParams(0,-1,1f))
        topBar.addView(speedTile,LinearLayout.LayoutParams(-2,-1).apply {marginStart=dp(10)})
        topBar.addView(settingsButton,LinearLayout.LayoutParams(-2,-1).apply {marginStart=dp(10)})
        topBar.addView(sessionActionHost,LinearLayout.LayoutParams(-2,-1).apply {marginStart=dp(10)})
        roadHost.addView(roadView,LayoutParams(-1,-1))
        roadHost.addView(label("ROAD VISION",12f).apply {typeface=CoachStyle.bold;letterSpacing=.1f;setPadding(dp(12),dp(7),dp(12),dp(7))
            background=CoachStyle.panel(context,Color.argb(200,9,13,19),10,Color.argb(80,255,255,255))},LayoutParams(-2,-2,Gravity.TOP or Gravity.START).apply {topMargin=dp(12);marginStart=dp(12)})
        roadHost.addView(roadStatus,LayoutParams(-2,-2,Gravity.BOTTOM or Gravity.START).apply {bottomMargin=dp(12);marginStart=dp(12);marginEnd=dp(150)})
        roadHost.addView(aimChip,LayoutParams(-2,-2,Gravity.BOTTOM or Gravity.END).apply {bottomMargin=dp(12);marginEnd=dp(12)})
        rightColumn.addView(topBar,LinearLayout.LayoutParams(-1,dp(54)))
        rightColumn.addView(roadHost,LinearLayout.LayoutParams(-1,0,1f).apply {topMargin=dp(10)})
        body.addView(leftColumn);body.addView(rightColumn)
        roadStatus.addTextChangedListener(object:TextWatcher {
            override fun beforeTextChanged(s:CharSequence?,a:Int,b:Int,c:Int)=Unit
            override fun onTextChanged(s:CharSequence?,a:Int,b:Int,c:Int)=Unit
            override fun afterTextChanged(s:Editable?){val v=if(s.isNullOrBlank())GONE else VISIBLE;if(roadStatus.visibility!=v)roadStatus.visibility=v}
        })
        // Setup sheet
        val sheet=LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;setPadding(dp(20),dp(16),dp(20),dp(16))}
        sheetHeader.addView(sheetHeading,LinearLayout.LayoutParams(0,-2,1f));sheetHeader.addView(closeSetupButton,LinearLayout.LayoutParams(dp(48),dp(48)));sheet.addView(sheetHeader,LinearLayout.LayoutParams(-1,dp(48)))
        tabs.addView(prepareTab,LinearLayout.LayoutParams(0,dp(44),1f));tabs.addView(destinationTab,LinearLayout.LayoutParams(0,dp(44),1f).apply {marginStart=dp(4)})
        sheet.addView(tabs,LinearLayout.LayoutParams(-1,dp(52)).apply {topMargin=dp(6);bottomMargin=dp(14)})
        val pages=FrameLayout(context).apply {addView(setupContentHost,LayoutParams(-1,-1));addView(destinationHost,LayoutParams(-1,-1))}
        sheet.addView(pages,LinearLayout.LayoutParams(-1,0,1f));sheet.addView(setupActionHost,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(10)})
        setupHost.addView(sheet,LayoutParams(-1,-1));addView(setupHost,LayoutParams(dp(440),-1,Gravity.END))
        for((kind,title) in listOf("camera" to "Camera","voice" to "Voice","location" to "GPS · optional","hill" to "Hills ahead")) {
            val symbol=icon(kind,CoachStyle.muted);val heading=label(title,14f).apply {typeface=CoachStyle.medium};val detail=label("Checking…",12f,CoachStyle.muted).apply {maxLines=1;ellipsize=TextUtils.TruncateAt.END}
            readiness.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;setPadding(dp(12),dp(6),dp(12),dp(6));background=CoachStyle.panel(context,CoachStyle.raised,12,Color.TRANSPARENT)
                addView(symbol,LinearLayout.LayoutParams(dp(24),dp(24)).apply {marginEnd=dp(14)})
                addView(LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;addView(heading);addView(detail,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(3)})},LinearLayout.LayoutParams(0,-2,1f))},LinearLayout.LayoutParams(-1,dp(54)).apply {bottomMargin=dp(6)})
            checks.add(Check(detail,symbol))
        }
        applyTurnStyle(false);setCoachCard(DriveCards.parked("Set up while parked","Choose a destination, or practise without one."));applyMapMode()
    }

    // ---- the route panel
    fun attachMap(view:View){if(mapHost.childCount==1&&mapHost.getChildAt(0)===view)return;mapHost.removeAllViews();mapHost.addView(view,LayoutParams(-1,-1))}
    /** Parked: the map (route preview). Driving: the turn card. */
    fun setMapMode(map:Boolean){if(mapMode==map)return;mapMode=map;applyMapMode()}
    private fun applyMapMode() {
        mapHost.visibility=if(mapMode)VISIBLE else GONE;turnCard.visibility=if(mapMode)GONE else VISIBLE
        // Driving: the turn card needs ~150 dp for two lines of instruction and the step after it.
        // Parked: the map's route preview and the coach card share the column.
        (routePanel.layoutParams as? LinearLayout.LayoutParams)?.let {it.weight=if(mapMode).48f else .42f;routePanel.layoutParams=it}
        (coachCard.layoutParams as? LinearLayout.LayoutParams)?.let {it.weight=if(mapMode).52f else .58f;coachCard.layoutParams=it}
    }
    private fun applyTurnStyle(guiding:Boolean) {
        if(guidingStyle==guiding)return;guidingStyle=guiding
        turnCard.background=if(guiding)CoachStyle.gradient(context,Color.rgb(17,133,94),CoachStyle.turnDeep,20,CoachStyle.turnLine)
            else CoachStyle.panel(context,CoachStyle.raised,20,CoachStyle.line)
        turnTile.background=CoachStyle.panel(context,if(guiding)Color.argb(110,0,40,28) else CoachStyle.surface,14,if(guiding)Color.argb(70,255,255,255) else CoachStyle.line)
        turnDivider.setBackgroundColor(if(guiding)Color.argb(60,255,255,255) else CoachStyle.line)
    }
    /** The next turn. Null: free practice (the coach still reads the road). */
    fun setTurnCard(card:TurnCard?) {
        val c=card?:TurnCard(null,"Free practice",null,"location",null,null,null,false)
        applyTurnStyle(c.guiding)
        maneuverDistance.setTextIfChanged(c.distance.orEmpty());maneuverDistance.visibility=if(c.distance==null)GONE else VISIBLE
        maneuverText.setTextIfChanged(c.instruction)
        maneuverText.textSize=if(c.distance==null)19f else 17f
        shield.setTextIfChanged(c.shield.orEmpty());shield.visibility=if(c.shield==null)GONE else VISIBLE
        if(c.icon!=turnIconKind){turnIconKind=c.icon;turnIcon.setImageDrawable(CoachStyle.icon(context,c.icon,if(c.guiding)Color.WHITE else CoachStyle.amber,2.6f))}
        thenText.setTextIfChanged(c.then.orEmpty());thenChip.visibility=if(c.then==null)GONE else VISIBLE
        val ti=c.thenIcon?:"straight";if(ti!=thenIconKind){thenIconKind=ti;thenIcon.setImageDrawable(CoachStyle.icon(context,ti,Color.WHITE,2.4f))}
        maneuverProvider.setTextIfChanged(c.attribution.orEmpty());maneuverProvider.contentDescription=c.attribution
        maneuverProvider.setTextColor(if(c.guiding)Color.WHITE else CoachStyle.muted)
        navigationStatus.visibility=if(c.guiding||navigationStatus.text.isNullOrBlank())GONE else VISIBLE
        val bottom=if(c.then!=null||!c.attribution.isNullOrBlank())VISIBLE else GONE
        turnDivider.visibility=bottom;(maneuverProvider.parent as View).visibility=bottom
    }
    /** Older callers: a plain instruction and distance, shown as a non-guiding card. */
    fun setManeuver(instruction:String?,distanceMetres:Double?=null) {
        val text=instruction?.takeIf {it.isNotBlank()}?:"Free practice"
        val kind=when {text.contains("left",true)->"left";text.contains("right",true)->"right";text.contains("straight",true)->"straight"
            text.contains("bump",true)->"bump";text.contains("junction",true)->"location";else->"location"}
        setTurnCard(TurnCard(DriveCards.distance(distanceMetres),text,null,kind,null,null,maneuverProvider.text?.toString()?.takeIf {it.isNotBlank()&&it!="Google Maps"},false))
    }
    fun setRouteCardVisible(@Suppress("UNUSED_PARAMETER") visible:Boolean)=Unit
    fun setProviderLabel(value:String?){maneuverProvider.setTextIfChanged(value.orEmpty())}

    // ---- the coach card
    fun setCoachCard(card:CoachCard) {
        val key=card.toString();if(key==coachKey)return;coachKey=key
        val tint=when(card.tone){CoachTone.ROUTE->CoachStyle.blue;CoachTone.ROAD_USER->CoachStyle.amber;CoachTone.URGENT->CoachStyle.stop
            CoachTone.ROAD->Color.rgb(34,211,238);CoachTone.CONTROL->CoachStyle.go;CoachTone.CALM->CoachStyle.go;CoachTone.PARKED->CoachStyle.amber}
        val quiet=card.tone==CoachTone.CALM||card.tone==CoachTone.PARKED
        coachCard.background=CoachStyle.panel(context,if(card.tone==CoachTone.URGENT)Color.rgb(36,10,16) else CoachStyle.coachNavy,20,
            if(quiet)CoachStyle.line else Color.argb(170,Color.red(tint),Color.green(tint),Color.blue(tint)))
        coachDot.setColor(tint);coachContext.setTextColor(tint);coachContext.setTextIfChanged(card.header)
        coachTile.background=CoachStyle.panel(context,if(quiet)CoachStyle.raised else tint,16,if(quiet)CoachStyle.line else Color.TRANSPARENT)
        coachIconKind=card.icon;coachIcon.setImageDrawable(CoachStyle.icon(context,card.icon,if(quiet)tint else CoachStyle.ink,2.5f))
        coachText.setTextIfChanged(card.title)
        if(card.sentence){coachText.typeface=CoachStyle.bold;coachText.maxLines=3;coachText.setAutoSizeTextTypeUniformWithConfiguration(14,22,1,android.util.TypedValue.COMPLEX_UNIT_SP)}
        else {coachText.typeface=CoachStyle.display;coachText.maxLines=2;coachText.setAutoSizeTextTypeUniformWithConfiguration(16,32,1,android.util.TypedValue.COMPLEX_UNIT_SP)}
        coachText.setTextColor(if(card.tone==CoachTone.URGENT)Color.rgb(255,214,219) else CoachStyle.text)
        coachSubtitle.setTextIfChanged(card.subtitle);coachSubtitle.visibility=if(card.subtitle.isBlank())GONE else VISIBLE
        coachSubtitle.setTextColor(if(quiet)CoachStyle.muted else CoachStyle.cream)
        chipRow.removeAllViews()
        card.chips.forEachIndexed {i,c->chipRow.addView(CoachStyle.chip(context,c.text,c.active,tint),LinearLayout.LayoutParams(-2,-2).apply {if(i>0)marginStart=dp(8)})}
        updateChipRow()
    }
    private fun updateChipRow() {
        val show=chipRow.childCount>0||quickStartButton.visibility==VISIBLE
        chipDivider.visibility=if(show)VISIBLE else GONE;(quickStartButton.parent as View).visibility=if(show)VISIBLE else GONE
    }
    /** Older callers: the instruction and where it came from. */
    fun setCoachPresentation(instruction:String,context:String){setCoachCard(CoachCard(context.uppercase(),CoachTone.CALM,instruction,"","coach",emptyList(),true))}
    /** The gear that suits the measured speed (advice, never the gear the car is in). */
    fun setSuggestedGear(gear:Int?) {
        val g=gear?.takeIf {it in 1..6};gearChip.setTextIfChanged(g?.let {"Gear $it · suggested"}.orEmpty())
        gearChip.contentDescription=g?.let {"Suggested gear for this speed: $it"};gearChip.visibility=if(g==null)GONE else VISIBLE
    }

    // ---- the top bar and the strip
    fun setDestination(label:String?) {
        val name=label?.takeIf {it.isNotBlank()}
        destinationName.setTextIfChanged(name?:"Where to?");destinationName.setTextColor(if(name==null)CoachStyle.muted else CoachStyle.text)
        if(name==null){tripRow.visibility=GONE;lockIcon.visibility=GONE}
    }
    /** What is left of the trip, from Google's feed (memory only). */
    fun setTrip(remainingMetres:Double?,remainingSeconds:Double?) {
        val km=remainingMetres?.takeIf {it.isFinite()&&it>=0}?.let {if(it>=1000)"%.1f km".format(it/1000) else "${(it/10).roundToInt()*10} m"}
        val min=remainingSeconds?.takeIf {it.isFinite()&&it>=0}?.let {val m=(it/60).roundToInt();if(m>=60)"${m/60} h ${m%60} min" else "${maxOf(1,m)} min"}
        tripDistance.setTextIfChanged(km.orEmpty());tripTime.setTextIfChanged(min.orEmpty())
        val v=if(km==null||min==null||destinationName.text=="Where to?")GONE else VISIBLE;if(tripRow.visibility!=v)tripRow.visibility=v
    }
    /** While driving Setup is locked: an icon is enough, and leaves the destination its room. */
    fun setSetupCompact(compact:Boolean) {
        settingsButton.compoundDrawablePadding=if(compact)0 else dp(8);settingsButton.setPadding(dp(if(compact)13 else 16),0,dp(if(compact)13 else 16),0)
        settingsButton.contentDescription="Setup"
    }
    fun setMeasuredSpeed(kph:Double?,source:String="GPS") {
        val valid=kph?.takeIf {it.isFinite()&&it in 0.0..300.0};speedValue.setTextIfChanged(valid?.roundToInt()?.toString()?:"—")
        speedSource.setTextIfChanged(if(valid==null)"No speed" else source);gpsDot.setColor(if(valid==null)CoachStyle.muted else CoachStyle.go)
    }
    /** A posted speed limit, only when a source gives one; never guessed. */
    fun setSpeedLimit(kph:Int?){val v=kph?.takeIf {it in 5..150};speedLimit.setTextIfChanged(v?.toString().orEmpty());speedLimit.visibility=if(v==null)GONE else VISIBLE}
    fun setMeasuredGear(@Suppress("UNUSED_PARAMETER") gear:Int?,@Suppress("UNUSED_PARAMETER") evidence:String?=null)=Unit
    fun setRoadName(value:String?){roadName.setTextIfChanged(value.orEmpty());roadName.visibility=if(value.isNullOrBlank())GONE else VISIBLE}
    fun setBattery(percent:Int?,charging:Boolean=false) {
        val p=percent?.takeIf {it in 0..100};battery.setTextIfChanged(p?.let {if(charging)"⚡$it%" else "$it%"}.orEmpty());battery.visibility=if(p==null)GONE else VISIBLE
        battery.setTextColor(if(p!=null&&p<=15&&!charging)CoachStyle.stop else CoachStyle.go)
    }
    /** How to re-aim the camera, when it can be acted on (parked or crawling). */
    fun setAimAdvice(value:String?){aimChip.setTextIfChanged(value.orEmpty());aimChip.visibility=if(value.isNullOrBlank())GONE else VISIBLE}

    // ---- setup and session
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
    fun setSessionRunning(value:Boolean){if(running==value)return;running=value;quickStartButton.visibility=if(value)GONE else VISIBLE;updateChipRow()}
    fun setReadiness(index:Int,detail:String,ready:Boolean) {
        val check=checks[index];val key="$detail:$ready";if(check.key==key)return;check.key=key;check.detail.setTextIfChanged(detail)
        check.symbol.setImageDrawable(CoachStyle.icon(context,if(ready)"check" else listOf("camera","voice","location","hill")[index],if(ready)CoachStyle.go else CoachStyle.muted))
    }
    override fun onMeasure(widthMeasureSpec:Int,heightMeasureSpec:Int){updateChrome(MeasureSpec.getSize(widthMeasureSpec),MeasureSpec.getSize(heightMeasureSpec));super.onMeasure(widthMeasureSpec,heightMeasureSpec)}
    private fun updateChrome(w:Int,h:Int) {
        if(w<=0||h<=0)return
        val availableW=(w-paddingLeft-paddingRight).coerceAtLeast(0);val open=setupHost.visibility==VISIBLE;val landscape=w>=h
        val key="$availableW:$h:$open:$keyboardVisible:$landscape";if(key==layoutKey)return;layoutKey=key
        // The camera side keeps its place and size whether or not Setup is open: the sheet slides over
        // it. A hidden camera preview never gets a picture surface, so the camera times out (error 4),
        // and the analysis crop follows the preview's shape (0.8.1), so it must not change either.
        val leftW=if(landscape)maxOf(dp(300),(availableW*.39f).roundToInt()).coerceAtMost((availableW-dp(212)).coerceAtLeast(0)) else -1
        val sheetWidth=if(landscape)(availableW-leftW-dp(12)).coerceAtLeast(minOf(availableW,dp(320))) else availableW
        setupHost.layoutParams=(setupHost.layoutParams as LayoutParams).apply {width=sheetWidth;height=-1;gravity=Gravity.END}
        strip.visibility=if(keyboardVisible)GONE else VISIBLE
        coachCard.visibility=if(keyboardVisible)GONE else VISIBLE
        setupActionHost.visibility=if(destinationPage||keyboardVisible)GONE else VISIBLE
        sheetHeader.visibility=if(keyboardVisible)GONE else VISIBLE;tabs.visibility=if(keyboardVisible)GONE else VISIBLE
        quickStartButton.visibility=if(running||open)GONE else VISIBLE;updateChipRow()
        body.orientation=if(landscape)LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
        rightColumn.visibility=VISIBLE
        if(landscape) {
            leftColumn.layoutParams=LinearLayout.LayoutParams(leftW,-1)
            rightColumn.layoutParams=LinearLayout.LayoutParams(0,-1,1f).apply {marginStart=dp(12)}
        } else {
            leftColumn.layoutParams=LinearLayout.LayoutParams(-1,0,1f)
            rightColumn.layoutParams=LinearLayout.LayoutParams(-1,0,1f).apply {topMargin=dp(10)}
        }
    }
    private fun TextView.setTextIfChanged(value:CharSequence){if(!TextUtils.equals(text,value))text=value}
}
