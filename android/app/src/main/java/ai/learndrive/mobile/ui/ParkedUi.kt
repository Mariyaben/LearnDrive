package ai.learndrive.mobile.ui

import ai.learndrive.mobile.CoachingProfile
import ai.learndrive.mobile.ParkedFlow
import ai.learndrive.mobile.ParkedFlow.Step
import android.content.Context
import android.text.InputType
import android.text.TextUtils
import android.view.Gravity
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.*

/*
 * What a learner sees while parked (0.10.0), apart from the drive screen itself: the first-open
 * questions (Onboarding), the sheet's "Get ready" page (BeforeYouDrive) and the Progress, Settings
 * and About pages (Page). Same night-road style as the drive screen (CoachStyle).
 */

/** A column no wider than [max]: lines across a whole landscape screen are hard to read. */
class Capped(context:Context,private val max:Int):LinearLayout(context) {
    init {orientation=VERTICAL}
    override fun onMeasure(widthSpec:Int,heightSpec:Int) {
        val size=MeasureSpec.getSize(widthSpec)
        super.onMeasure(if(size>max)MeasureSpec.makeMeasureSpec(max,MeasureSpec.EXACTLY) else widthSpec,heightSpec)
    }
}

/** One of a few choices, all shown at once (never a drop-down), the chosen one lit amber like the
 * sheet's tabs: in a row, a column ([vertical]) or rows of [columns]. [onPick] runs only when the
 * learner picks another one. */
class Choice(context:Context,labels:List<String>,height:Int=44,textSize:Float=14f,vertical:Boolean=false,columns:Int=0,
    private val onPick:(Int)->Unit):LinearLayout(context) {
    private fun dp(v:Int)=CoachStyle.dp(context,v)
    var selected=-1;private set
    private val buttons:List<Button> = labels.mapIndexed {i,label->CoachStyle.button(context,label).apply {
        CoachStyle.styleTab(this);this.textSize=textSize;maxLines=2;setPadding(dp(8),0,dp(8),0)
        setOnClickListener {if(selected!=i){select(i);onPick(i)}}
    }}
    init {
        background=CoachStyle.panel(context,CoachStyle.ink,14,CoachStyle.line);setPadding(dp(4),dp(4),dp(4),dp(4))
        when {
            columns>0->{
                orientation=VERTICAL
                buttons.chunked(columns).forEachIndexed {r,row->addView(LinearLayout(context).apply {
                    row.forEachIndexed {i,b->addView(b,LayoutParams(0,dp(height),1f).apply {if(i>0)marginStart=dp(4)})}
                    // A short last row keeps its buttons the same width as the rows above.
                    repeat(columns-row.size) {addView(View(context),LayoutParams(0,dp(height),1f).apply {marginStart=dp(4)})}
                },LayoutParams(-1,-2).apply {if(r>0)topMargin=dp(4)})}
            }
            vertical->{orientation=VERTICAL;buttons.forEachIndexed {i,b->addView(b,LayoutParams(-1,dp(height)).apply {if(i>0)topMargin=dp(4)})}}
            else->{orientation=HORIZONTAL;buttons.forEachIndexed {i,b->addView(b,LayoutParams(-2,dp(height),1f).apply {if(i>0)marginStart=dp(4)})}}
        }
    }
    fun select(index:Int){selected=index;buttons.forEachIndexed {i,b->b.isSelected=i==index}}
    override fun setEnabled(enabled:Boolean){super.setEnabled(enabled);buttons.forEach {it.isEnabled=enabled}}
}

private fun caption(context:Context,text:String)=CoachStyle.eyebrow(context,text,CoachStyle.amber)
private fun note(context:Context,text:String="",color:Int=CoachStyle.muted,size:Float=13f)=CoachStyle.label(context,text,size,color).apply {setLineSpacing(0f,1.2f)}

/**
 * The sheet's "Get ready" page: what a learner chooses before every drive, all on show (coaching
 * level first, then the coach's voice and today's focus), whether the phone is ready, and the way
 * to Progress, Settings and About. The activity fills [checks] and wires the buttons.
 */
class BeforeYouDrive(context:Context,levels:List<String>,voices:List<String>,focuses:List<String>,
    onLevel:(Int)->Unit,onVoice:(Int)->Unit,onFocus:(Int)->Unit):LinearLayout(context) {
    private fun dp(v:Int)=CoachStyle.dp(context,v)
    val level=Choice(context,levels,onPick=onLevel).apply {tag="coaching-level"}
    val levelLine=note(context,color=CoachStyle.text)
    // English or Malayalam, recorded or the phone's own voice: two by two.
    val voice=Choice(context,voices,textSize=13f,columns=2,onPick=onVoice).apply {tag="voice-language"}
    val hear=CoachStyle.button(context,"Hear it").apply {tag="hear-voice";compoundDrawablePadding=dp(6);setPadding(dp(12),0,dp(14),0)
        setCompoundDrawablesRelativeWithIntrinsicBounds(CoachStyle.icon(context,"voice",CoachStyle.text),null,null,null)}
    val voiceNote=note(context).apply {visibility=GONE}
    val getVoice=CoachStyle.button(context,"Get the Malayalam voice").apply {visibility=GONE}
    val focus=Choice(context,focuses,textSize=13f,columns=3,onPick=onFocus).apply {tag="drive-focus"}
    val focusLine=note(context)
    val checks=LinearLayout(context).apply {orientation=VERTICAL}
    val progress=CoachStyle.button(context,"Progress").apply {tag="open-progress"}
    val settings=CoachStyle.button(context,"Settings").apply {tag="open-settings"}
    val about=CoachStyle.button(context,"About").apply {tag="open-about"}
    init {
        orientation=VERTICAL
        heading("Coaching level",0);addView(level,LayoutParams(-1,dp(52)));addView(levelLine,LayoutParams(-1,-2).apply {topMargin=dp(6)})
        heading("Coach's voice")
        addView(voice,LayoutParams(-1,-2))
        addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL
            addView(hear,LayoutParams(-2,dp(46)));addView(voiceNote,LayoutParams(0,-2,1f).apply {marginStart=dp(10)})},LayoutParams(-1,-2).apply {topMargin=dp(8)})
        addView(getVoice,LayoutParams(-1,dp(46)).apply {topMargin=dp(6)})
        heading("Today's focus");addView(focus,LayoutParams(-1,-2));addView(focusLine,LayoutParams(-1,-2).apply {topMargin=dp(6)})
        heading("Ready to drive");addView(checks,LayoutParams(-1,-2))
        addView(LinearLayout(context).apply {
            addView(progress,LayoutParams(0,dp(46),1f));addView(settings,LayoutParams(0,dp(46),1f).apply {marginStart=dp(6)})
            addView(about,LayoutParams(0,dp(46),1f).apply {marginStart=dp(6)})},LayoutParams(-1,-2).apply {topMargin=dp(10);bottomMargin=dp(6)})
    }
    private fun heading(text:String,top:Int=18)=addView(caption(context,text),LayoutParams(-1,-2).apply {topMargin=dp(top);bottomMargin=dp(7)})
}

/** A full-screen page over the parked screen (Progress, Settings, About): its title and a close
 * button, then sections in a column that reads comfortably. Built afresh each time it opens. */
class Page(context:Context,title:String,private val onClose:()->Unit):FrameLayout(context) {
    private fun dp(v:Int)=CoachStyle.dp(context,v)
    val column=Capped(context,CoachStyle.dp(context,640))
    init {
        setBackgroundColor(CoachStyle.ink);isClickable=true;isFocusable=true;tag="page"
        val heading=CoachStyle.label(context,title,26f).apply {typeface=CoachStyle.display;maxLines=1;ellipsize=TextUtils.TruncateAt.END}
        val close=CoachStyle.button(context,"").apply {contentDescription="Close $title";setPadding(dp(12),0,dp(12),0)
            setCompoundDrawablesRelativeWithIntrinsicBounds(CoachStyle.icon(context,"close"),null,null,null);setOnClickListener {onClose()}}
        column.setPadding(dp(20),dp(12),dp(20),dp(28))
        column.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL
            addView(heading,LinearLayout.LayoutParams(0,-2,1f));addView(close,LinearLayout.LayoutParams(dp(48),dp(48)))},LinearLayout.LayoutParams(-1,-2))
        val frame=FrameLayout(context).apply {addView(column,LayoutParams(-1,-2,Gravity.CENTER_HORIZONTAL))}
        addView(ScrollView(context).apply {isVerticalScrollBarEnabled=false;isFillViewport=true;addView(frame)},LayoutParams(-1,-1))
    }
    fun section(title:String)=caption(context,title).also {column.addView(it,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(24);bottomMargin=dp(8)})}
    fun paragraph(text:String,color:Int=CoachStyle.text,size:Float=15f)=
        note(context,text,color,size).also {column.addView(it,LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=dp(8)})}
    fun <T:View> add(view:T,height:Int=-2,top:Int=6):T=view.also {column.addView(it,LinearLayout.LayoutParams(-1,height).apply {topMargin=dp(top)})}
    fun button(text:String,primary:Boolean=false,action:()->Unit)=
        add(CoachStyle.button(context,text,primary).apply {setOnClickListener {action()}},dp(48))
    fun switch(text:String,on:Boolean,change:(Boolean)->Unit)=add(Switch(context).apply {
        this.text=text;textSize=16f;setTextColor(CoachStyle.text);isChecked=on;minHeight=dp(48)
        setOnCheckedChangeListener {_,checked->change(checked)}
    })
}

/**
 * First open, once: one question per screen ([Step]). The last one, mounting the phone, leaves the
 * road camera on the right in view. Taps never reach the drive screen beneath.
 */
class Onboarding(context:Context,private val host:Host):FrameLayout(context) {
    /** What the questions read and change: the activity's own state. */
    interface Host {
        /** The coach's voices (a language, recorded or the phone's own), as a learner reads them; the chosen one. */
        fun voices():List<String>
        fun voice():Int
        fun setVoice(index:Int)
        fun hear()
        fun voiceNote():String?
        fun canGetVoice():Boolean
        fun getVoice()
        fun name():String
        /** False when the name cannot be kept (empty, too long, or another learner's). */
        fun rename(raw:String):Boolean
        fun manual():Boolean
        fun setManual(on:Boolean)
        fun profile():CoachingProfile
        fun setProfile(profile:CoachingProfile)
        /** What the camera sees now, and whether that is ready. */
        fun camera():Pair<String,Boolean>
        fun needsPermission():Boolean
        fun allow()
        /** How wide the drive screen's left column is (0 before layout). */
        fun leftColumnWidth():Int
        fun finished()
    }
    private fun dp(v:Int)=CoachStyle.dp(context,v)
    var step=Step.WELCOME;private set
    private val card=FrameLayout(context).apply {setBackgroundColor(CoachStyle.ink)}
    private val column=Capped(context,CoachStyle.dp(context,600))
    private val scroll=ScrollView(context).apply {isVerticalScrollBarEnabled=false;isFillViewport=true
        addView(FrameLayout(context).apply {addView(column,LayoutParams(-1,-2,Gravity.CENTER_HORIZONTAL))})}
    // Lines that follow the phone while a question is open.
    private var voiceNote:TextView?=null
    private var getVoice:Button?=null
    private var cameraLine:TextView?=null
    private var cameraIcon:ImageView?=null
    private var allow:Button?=null
    private var nameInput:EditText?=null
    private var nameError:TextView?=null
    private var languageChoice:Choice?=null
    private var levelLine:TextView?=null

    init {
        tag="onboarding";isClickable=true;isFocusable=true
        card.addView(scroll,LayoutParams(-1,-1));addView(card,LayoutParams(-1,-1))
        show(Step.WELCOME)
    }

    /** The question before this one; false on the first. */
    fun back():Boolean {val previous=step.previous?:return false;show(previous);return true}

    private fun next() {
        if(step==Step.NAME) {
            val input=nameInput?.text?.toString().orEmpty()
            if(!host.rename(input)){nameError?.visibility=VISIBLE;return}
            hideKeyboard()
        }
        step.next?.let(::show)?:host.finished()
    }

    private fun show(s:Step) {
        step=s;column.removeAllViews()
        voiceNote=null;getVoice=null;cameraLine=null;cameraIcon=null;allow=null;nameInput=null;nameError=null;languageChoice=null;levelLine=null
        column.setPadding(dp(24),dp(18),dp(24),dp(20))
        column.addView(CoachStyle.eyebrow(context,"Step ${s.number} of ${Step.entries.size}",CoachStyle.amber),LinearLayout.LayoutParams(-1,-2))
        column.addView(CoachStyle.label(context,s.title,26f).apply {typeface=CoachStyle.display;setLineSpacing(0f,1.1f)},
            LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(8);bottomMargin=dp(10)})
        when(s) {
            Step.WELCOME->{
                body("A driving coach on your windscreen. Through your phone's rear camera it watches the road ahead and tells you, out loud, what to do next: when to slow down, how to take a bend or a turn, when a road user needs room. With a destination, it also gets you ready for every turn on the way.")
                column.addView(LinearLayout(context).apply {orientation=LinearLayout.VERTICAL;setPadding(dp(16),dp(14),dp(16),dp(14))
                    background=CoachStyle.panel(context,CoachStyle.raised,14,CoachStyle.line)
                    addView(CoachStyle.label(context,"Before you start",15f).apply {typeface=CoachStyle.bold})
                    for(line in listOf("Practise with a licensed driver beside you.","Set things up only while parked.",
                        "The coach can miss things, so keep watching the road. Silence never means the road is clear."))
                        addView(note(context,"•  $line",CoachStyle.text,15f),LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(6)})
                },LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(4)})
            }
            Step.LANGUAGE->{
                body("The coach speaks its instructions out loud: in English or Malayalam, in a real person's recorded voice or the phone's own friendly voice. The screens stay in English for now.")
                languageChoice=Choice(context,host.voices(),height=52,textSize=15f,columns=2) {i->host.setVoice(i);refresh()}.also {
                    it.select(host.voice())
                    column.addView(it,LinearLayout.LayoutParams(-1,-2))
                }
                column.addView(CoachStyle.button(context,"Hear it").apply {compoundDrawablePadding=dp(8)
                    setCompoundDrawablesRelativeWithIntrinsicBounds(CoachStyle.icon(context,"voice",CoachStyle.text),null,null,null);setOnClickListener {host.hear()}},
                    LinearLayout.LayoutParams(-2,dp(48)).apply {topMargin=dp(10)})
                voiceNote=note(context).also {column.addView(it,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(8)})}
                getVoice=CoachStyle.button(context,"Get the Malayalam voice").apply {setOnClickListener {host.getVoice()}}.also {
                    column.addView(it,LinearLayout.LayoutParams(-2,dp(48)).apply {topMargin=dp(6)})}
            }
            Step.NAME->{
                body("Each learner on this phone keeps their own progress and settings. You can add more learners later, in Settings.")
                nameInput=EditText(context).apply {
                    setText(host.name());setSelectAllOnFocus(true);isSingleLine=true;textSize=20f;setTextColor(CoachStyle.text);setHintTextColor(CoachStyle.muted)
                    hint="Your name";inputType=InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_WORDS;imeOptions=EditorInfo.IME_ACTION_NEXT
                    filters=arrayOf(android.text.InputFilter.LengthFilter(ai.learndrive.mobile.Learners.MAX_NAME))
                    background=CoachStyle.panel(context,CoachStyle.raised,14,CoachStyle.line);setPadding(dp(16),dp(12),dp(16),dp(12))
                    setOnEditorActionListener {_,action,_->if(action==EditorInfo.IME_ACTION_NEXT){next();true} else false}
                    addTextChangedListener(object:android.text.TextWatcher {
                        override fun beforeTextChanged(s:CharSequence?,a:Int,b:Int,c:Int)=Unit
                        override fun onTextChanged(s:CharSequence?,a:Int,b:Int,c:Int)=Unit
                        override fun afterTextChanged(s:android.text.Editable?){nameError?.visibility=GONE}
                    })
                }.also {column.addView(it,LinearLayout.LayoutParams(-1,dp(56)))}
                nameError=note(context,"Use 1 to ${ai.learndrive.mobile.Learners.MAX_NAME} characters, and a name no other learner on this phone has.",CoachStyle.stop).apply {visibility=GONE}
                    .also {column.addView(it,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(8)})}
            }
            Step.CAR->{
                body("In a manual car, the coach suggests the gear that suits your speed. It never sees which gear you are in.")
                column.addView(Choice(context,listOf("Manual","Automatic"),height=52,textSize=16f) {i->host.setManual(i==0)}.apply {select(if(host.manual())0 else 1)},
                    LinearLayout.LayoutParams(-1,dp(60)))
            }
            Step.LEVEL->{
                body("This sets how much the coach says. You can change it before any drive.")
                val answers=ParkedFlow.Experience.entries
                column.addView(Choice(context,answers.map {"${it.label}  ·  ${it.profile.label}"},height=50,textSize=16f,vertical=true) {i->
                    host.setProfile(answers[i].profile);levelLine?.text=ParkedFlow.levelLine(answers[i].profile)
                }.apply {select(answers.indexOf(ParkedFlow.Experience.of(host.profile())))},LinearLayout.LayoutParams(-1,-2))
                levelLine=note(context,ParkedFlow.levelLine(host.profile()),CoachStyle.text,15f).also {column.addView(it,LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(10)})}
            }
            Step.MOUNT->{
                body("Fix the phone firmly in landscape, low on the windscreen or dashboard, with the rear camera facing the road and your view of the road clear. The road camera shows on the right.")
                column.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL;setPadding(dp(14),dp(12),dp(14),dp(12))
                    background=CoachStyle.panel(context,CoachStyle.raised,14,CoachStyle.line)
                    cameraIcon=ImageView(context).also {addView(it,LinearLayout.LayoutParams(dp(24),dp(24)).apply {marginEnd=dp(12)})}
                    cameraLine=CoachStyle.label(context,"",15f).also {addView(it,LinearLayout.LayoutParams(0,-2,1f))}
                },LinearLayout.LayoutParams(-1,-2))
                allow=CoachStyle.button(context,"Allow camera and location",true).apply {setOnClickListener {host.allow()}}.also {
                    column.addView(it,LinearLayout.LayoutParams(-1,dp(52)).apply {topMargin=dp(10)})}
                column.addView(note(context,"The camera sees the road; location gives your speed and follows your route."),LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(8)})
            }
        }
        column.addView(LinearLayout(context).apply {gravity=Gravity.CENTER_VERTICAL
            if(s.previous!=null)addView(CoachStyle.button(context,"Back").apply {setOnClickListener {back()}},LinearLayout.LayoutParams(-2,dp(52)))
            addView(View(context),LinearLayout.LayoutParams(0,1,1f))
            addView(CoachStyle.button(context,when(s){Step.WELCOME->"Get started";Step.MOUNT->"Done";else->"Next"},true).apply {tag="onboarding-next";setOnClickListener {next()}},
                LinearLayout.LayoutParams(-2,dp(52)).apply {marginStart=dp(10)})
        },LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(22)})
        // Mounting the phone: this column on the left, the road camera on the right.
        card.layoutParams=if(s==Step.MOUNT)LayoutParams(host.leftColumnWidth().takeIf {it>0}?:(resources.displayMetrics.widthPixels*.4f).toInt(),-1,Gravity.START)
            else LayoutParams(-1,-1)
        scroll.scrollTo(0,0)
        refresh()
    }

    private fun body(text:String)=column.addView(note(context,text,CoachStyle.text,16f),LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=dp(14)})

    /** The lines that follow the phone: the voice, and what the camera sees. */
    fun refresh() {
        voiceNote?.let {v->val text=host.voiceNote();v.text=text.orEmpty();v.visibility=if(text==null)GONE else VISIBLE}
        getVoice?.visibility=if(host.canGetVoice())VISIBLE else GONE
        languageChoice?.let {c->val i=host.voice();if(i>=0&&c.selected!=i)c.select(i)}
        cameraLine?.let {line->
            val (text,ok)=host.camera()
            if(line.text.toString()!=text)line.text=text
            cameraIcon?.setImageDrawable(CoachStyle.icon(context,if(ok)"check" else "camera",if(ok)CoachStyle.go else CoachStyle.amber))
        }
        allow?.visibility=if(host.needsPermission())VISIBLE else GONE
    }

    private fun hideKeyboard() {
        nameInput?.let {it.clearFocus();context.getSystemService(InputMethodManager::class.java)?.hideSoftInputFromWindow(it.windowToken,0)}
    }
}
