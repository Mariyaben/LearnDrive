package ai.learndrive.mobile.ui

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.*
import android.graphics.drawable.*
import android.view.Gravity
import android.widget.Button
import android.widget.TextView
import kotlin.math.roundToInt

/**
 * Night-road navy: dark panels that keep the camera and the instructions the brightest things on
 * screen. Google's turn card is green (as its own banner is), the coach is blue, amber marks the
 * learner plate and anything to check, green means ready and red means act now. Bold geometric
 * type for anything read at a glance while driving. Local vector icons and system fonts only.
 */
object CoachStyle {
    val ink=Color.rgb(9,13,19)            // the page
    val surface=Color.rgb(14,21,32)       // panels
    val raised=Color.rgb(21,31,45)
    val line=Color.rgb(34,48,68)
    val text=Color.rgb(244,246,248)
    val muted=Color.rgb(143,155,173)
    val amber=Color.rgb(245,184,46)       // the learner plate, things to check
    val accent=amber
    val go=Color.rgb(52,211,153)
    val stop=Color.rgb(255,84,104)
    val blue=Color.rgb(56,189,248)        // the coach
    val cream=Color.rgb(242,213,138)      // the rest of an instruction
    val turnGreen=Color.rgb(15,122,87)    // the turn card
    val turnDeep=Color.rgb(10,92,65)
    val turnLine=Color.rgb(34,163,116)
    val coachNavy=Color.rgb(10,24,36)
    val coachLine=Color.rgb(29,91,130)
    val endRed=Color.rgb(64,14,26)
    val endLine=Color.rgb(122,32,51)
    val display:Typeface=Typeface.create("sans-serif-black",Typeface.NORMAL)
    val bold:Typeface=Typeface.create("sans-serif",Typeface.BOLD)
    val medium:Typeface=Typeface.create("sans-serif-medium",Typeface.NORMAL)
    fun dp(context:Context,value:Int)=(value*context.resources.displayMetrics.density).roundToInt()
    fun panel(context:Context,color:Int=surface,radius:Int=16,stroke:Int=line)=
        GradientDrawable().apply {setColor(color);cornerRadius=dp(context,radius).toFloat();if(stroke!=Color.TRANSPARENT)setStroke(dp(context,1),stroke)}
    /** A vertical two-colour card (the turn card's green). */
    fun gradient(context:Context,top:Int,bottom:Int,radius:Int=18,stroke:Int=Color.TRANSPARENT)=
        GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,intArrayOf(top,bottom)).apply {cornerRadius=dp(context,radius).toFloat();if(stroke!=Color.TRANSPARENT)setStroke(dp(context,1),stroke)}
    /** A card with a thick edge on the left (kept for the setup sheet). */
    fun voiceCard(context:Context,edge:Int=amber)=LayerDrawable(arrayOf(panel(context,raised,16,Color.TRANSPARENT),
        GradientDrawable().apply {setColor(edge);cornerRadii=floatArrayOf(dp(context,16).toFloat(),dp(context,16).toFloat(),0f,0f,0f,0f,dp(context,16).toFloat(),dp(context,16).toFloat())})).apply {
        setLayerWidth(1,dp(context,6));setLayerGravity(1,Gravity.START or Gravity.FILL_VERTICAL)
    }
    fun label(context:Context,value:String,size:Float=14f,color:Int=text)=TextView(context).apply {
        text=value;textSize=size;setTextColor(color);typeface=Typeface.create("sans-serif",Typeface.NORMAL);includeFontPadding=false
    }
    /** Small uppercase caption above a value. */
    fun eyebrow(context:Context,value:String,color:Int=muted)=label(context,value,10.5f,color).apply {letterSpacing=.12f;typeface=bold;isAllCaps=true}
    /** A rounded tag: [active] ones are lit in [tint]. */
    fun chip(context:Context,value:String,active:Boolean=false,tint:Int=blue)=label(context,value,12f,if(active)lighten(tint) else Color.rgb(201,211,223)).apply {
        typeface=medium;maxLines=1;setPadding(dp(context,10),dp(context,5),dp(context,10),dp(context,5))
        background=panel(context,if(active)Color.argb(70,Color.red(tint),Color.green(tint),Color.blue(tint)) else raised,9,if(active)Color.argb(150,Color.red(tint),Color.green(tint),Color.blue(tint)) else line)
    }
    fun lighten(color:Int,amount:Float=.45f)=Color.rgb((Color.red(color)+(255-Color.red(color))*amount).toInt(),
        (Color.green(color)+(255-Color.green(color))*amount).toInt(),(Color.blue(color)+(255-Color.blue(color))*amount).toInt())
    fun styleButton(button:Button,primary:Boolean=false)=button.apply {
        isAllCaps=false;textSize=15f;typeface=if(primary)bold else medium;letterSpacing=0f
        gravity=Gravity.CENTER;minimumHeight=dp(context,48);minHeight=dp(context,48);minimumWidth=0;minWidth=0
        setPadding(dp(context,16),0,dp(context,16),0);stateListAnimator=null
        setTextColor(ColorStateList(arrayOf(intArrayOf(-android.R.attr.state_enabled),intArrayOf()),
            intArrayOf(Color.rgb(95,106,122),if(primary)ink else CoachStyle.text)))
        val states=StateListDrawable().apply {
            addState(intArrayOf(-android.R.attr.state_enabled),panel(context,if(primary)line else surface,14,if(primary)Color.TRANSPARENT else line))
            addState(intArrayOf(android.R.attr.state_selected),panel(context,if(primary)amber else raised,14,if(primary)Color.TRANSPARENT else amber))
            addState(intArrayOf(),panel(context,if(primary)amber else surface,14,if(primary)Color.TRANSPARENT else line))
        }
        background=RippleDrawable(ColorStateList.valueOf(if(primary)Color.argb(60,0,0,0) else Color.argb(40,56,189,248)),states,null)
    }
    /** The red "End" button: act-now red, kept dark so it does not shout while driving. */
    fun styleEnd(button:Button)=button.apply {
        styleButton(this);typeface=bold
        setCompoundDrawablesRelativeWithIntrinsicBounds(DotIcon(context,stop,9),null,null,null);compoundDrawablePadding=dp(context,8)
        background=RippleDrawable(ColorStateList.valueOf(Color.argb(60,255,84,104)),panel(context,endRed,14,endLine),null)
    }
    /** Segmented-control tab: the selected one sits on amber. */
    fun styleTab(button:Button)=button.apply {
        styleButton(this);typeface=medium
        setTextColor(ColorStateList(arrayOf(intArrayOf(android.R.attr.state_selected),intArrayOf()),intArrayOf(ink,muted)))
        background=RippleDrawable(ColorStateList.valueOf(Color.argb(30,245,184,46)),StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_selected),panel(context,amber,12,Color.TRANSPARENT))
            addState(intArrayOf(),panel(context,surface,12,Color.TRANSPARENT))
        },null)
    }
    fun button(context:Context,value:String,primary:Boolean=false)=styleButton(Button(context).apply {text=value},primary)
    fun icon(context:Context,kind:String,color:Int=text,weight:Float=2.1f)=CoachIcon(context,kind,color,weight)
}

/** A filled dot of [sizeDp], for status lights. */
class DotIcon(private val context:Context,color:Int,private val sizeDp:Int=8):Drawable() {
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG).apply {this.color=color}
    override fun getIntrinsicWidth()=CoachStyle.dp(context,sizeDp)
    override fun getIntrinsicHeight()=CoachStyle.dp(context,sizeDp)
    override fun draw(canvas:Canvas){canvas.drawCircle(bounds.exactCenterX(),bounds.exactCenterY(),minOf(bounds.width(),bounds.height())/2f,paint)}
    fun setColor(color:Int){if(paint.color!=color){paint.color=color;invalidateSelf()}}
    override fun setAlpha(alpha:Int){paint.alpha=alpha;invalidateSelf()}
    override fun setColorFilter(colorFilter:ColorFilter?){paint.colorFilter=colorFilter;invalidateSelf()}
    @Deprecated("Deprecated in Java") override fun getOpacity()=PixelFormat.TRANSLUCENT
}

class CoachIcon(private val context:Context,private val kind:String,color:Int,weight:Float=2.1f):Drawable() {
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG).apply {this.color=color;style=Paint.Style.STROKE;strokeWidth=weight;strokeCap=Paint.Cap.ROUND;strokeJoin=Paint.Join.ROUND}
    private val fill=Paint(Paint.ANTI_ALIAS_FLAG).apply {this.color=color;style=Paint.Style.FILL}
    override fun getIntrinsicWidth()=CoachStyle.dp(context,24)
    override fun getIntrinsicHeight()=CoachStyle.dp(context,24)
    override fun draw(canvas:Canvas) {
        val save=canvas.save();canvas.translate(bounds.left.toFloat(),bounds.top.toFloat());canvas.scale(bounds.width()/24f,bounds.height()/24f)
        val p=Path()
        when(kind) {
            "search"->{canvas.drawCircle(10.5f,10.5f,5.7f,paint);canvas.drawLine(15f,15f,20f,20f,paint)}
            "close"->{canvas.drawLine(6f,6f,18f,18f,paint);canvas.drawLine(18f,6f,6f,18f,paint)}
            // The learner plate: an amber tile with a bold "L".
            "brand"->{fill.color=CoachStyle.amber;canvas.drawRoundRect(2f,2f,22f,22f,5f,5f,fill)
                val l=Paint(Paint.ANTI_ALIAS_FLAG).apply {color=CoachStyle.ink;style=Paint.Style.STROKE;strokeWidth=3.4f;strokeCap=Paint.Cap.SQUARE}
                p.moveTo(9f,6.5f);p.lineTo(9f,17f);p.lineTo(16.5f,17f);canvas.drawPath(p,l)}
            "check"->{p.moveTo(5f,12f);p.lineTo(10f,17f);p.lineTo(19f,7f);canvas.drawPath(p,paint)}
            "camera"->{canvas.drawRoundRect(3f,6f,21f,19f,3f,3f,paint);canvas.drawCircle(12f,12.5f,3.3f,paint);canvas.drawLine(7f,4f,12f,4f,paint)}
            "voice"->{p.moveTo(4f,10f);p.lineTo(8f,10f);p.lineTo(12f,6f);p.lineTo(12f,18f);p.lineTo(8f,14f);p.lineTo(4f,14f);p.close();canvas.drawPath(p,paint);canvas.drawArc(8f,5f,23f,19f,-50f,100f,false,paint);canvas.drawArc(11f,8f,20f,16f,-50f,100f,false,paint)}
            "left","right"->{if(kind=="left"){canvas.translate(24f,0f);canvas.scale(-1f,1f)};p.moveTo(6f,20f);p.lineTo(6f,12f);p.quadTo(6f,8f,10f,8f);p.lineTo(20f,8f);p.moveTo(15f,3f);p.lineTo(20f,8f);p.lineTo(15f,13f);canvas.drawPath(p,paint)}
            "straight"->{canvas.drawLine(12f,21f,12f,3f,paint);p.moveTo(6f,9f);p.lineTo(12f,3f);p.lineTo(18f,9f);canvas.drawPath(p,paint)}
            "uturn"->{p.moveTo(7f,21f);p.lineTo(7f,10f);p.arcTo(7f,4f,17f,14f,180f,180f,false);p.lineTo(17f,18f);p.moveTo(13.5f,14.5f);p.lineTo(17f,18f);p.lineTo(20.5f,14.5f);canvas.drawPath(p,paint)}
            "roundabout"->{canvas.drawCircle(12f,10f,4.2f,paint);canvas.drawLine(12f,14.2f,12f,21f,paint);canvas.drawLine(15f,7f,19.5f,2.5f,paint)
                p.moveTo(15.5f,2.5f);p.lineTo(19.5f,2.5f);p.lineTo(19.5f,6.5f);canvas.drawPath(p,paint)}
            "flag"->{canvas.drawLine(6f,21f,6f,3.5f,paint);p.moveTo(6f,4f);p.lineTo(18.5f,4f);p.lineTo(15.5f,8f);p.lineTo(18.5f,12f);p.lineTo(6f,12f);canvas.drawPath(p,paint)}
            "person"->{canvas.drawCircle(12f,4.8f,2.3f,paint);canvas.drawLine(12f,8.5f,12f,14.5f,paint);canvas.drawLine(7.5f,11.5f,16.5f,11.5f,paint)
                canvas.drawLine(12f,14.5f,8.5f,21f,paint);canvas.drawLine(12f,14.5f,15.5f,21f,paint)}
            "bike"->{canvas.drawCircle(5.8f,16f,3.6f,paint);canvas.drawCircle(18.2f,16f,3.6f,paint);p.moveTo(5.8f,16f);p.lineTo(10f,9.5f);p.lineTo(15f,9.5f);p.lineTo(18.2f,16f)
                p.moveTo(15f,9.5f);p.lineTo(14f,6f);p.lineTo(17f,6f);canvas.drawPath(p,paint)}
            "car"->{canvas.drawRoundRect(3f,10.5f,21f,17f,2.5f,2.5f,paint);p.moveTo(6f,10.5f);p.lineTo(8f,6f);p.lineTo(16f,6f);p.lineTo(18f,10.5f);canvas.drawPath(p,paint)
                canvas.drawCircle(7.5f,17.5f,1.9f,fill);canvas.drawCircle(16.5f,17.5f,1.9f,fill)}
            "bendLeft","bendRight"->{if(kind=="bendRight"){canvas.translate(24f,0f);canvas.scale(-1f,1f)};p.moveTo(16f,21f);p.quadTo(16f,8f,6.5f,6.5f)
                p.moveTo(10f,3f);p.lineTo(6.5f,6.5f);p.lineTo(10f,10f);canvas.drawPath(p,paint)}
            "bump"->{canvas.drawLine(2.5f,18f,21.5f,18f,paint);p.moveTo(6f,18f);p.quadTo(12f,7f,18f,18f);canvas.drawPath(p,paint)}
            "lane"->{canvas.drawLine(7f,3f,4.5f,21f,paint);canvas.drawLine(17f,3f,19.5f,21f,paint)
                for(y in listOf(3.5f,10f,16.5f))canvas.drawLine(12f,y,12f,y+3.5f,paint)}
            // A manual gearbox's shift pattern.
            "gear"->{canvas.drawLine(6f,5f,6f,19f,paint);canvas.drawLine(12f,5f,12f,19f,paint);canvas.drawLine(18f,5f,18f,12f,paint);canvas.drawLine(6f,12f,18f,12f,paint)
                for((x,y) in listOf(6f to 5f,12f to 5f,18f to 5f,6f to 19f,12f to 19f))canvas.drawCircle(x,y,1.6f,fill)}
            "pin"->{canvas.drawCircle(12f,9.5f,5.6f,paint);canvas.drawCircle(12f,9.5f,1.8f,fill);canvas.drawLine(7.4f,12.6f,12f,20.5f,paint);canvas.drawLine(16.6f,12.6f,12f,20.5f,paint)}
            "lock"->{canvas.drawRoundRect(6f,11f,18f,20f,2f,2f,paint);p.moveTo(8.5f,11f);p.lineTo(8.5f,8f);p.arcTo(8.5f,4.5f,15.5f,11.5f,180f,180f,false);p.lineTo(15.5f,11f);canvas.drawPath(p,paint)}
            "eye"->{p.moveTo(2.5f,12f);p.quadTo(12f,3.5f,21.5f,12f);p.quadTo(12f,20.5f,2.5f,12f);p.close();canvas.drawPath(p,paint);canvas.drawCircle(12f,12f,2.8f,paint)}
            "warning"->{p.moveTo(12f,3f);p.lineTo(22f,20f);p.lineTo(2f,20f);p.close();canvas.drawPath(p,paint);canvas.drawLine(12f,9f,12f,14f,paint);canvas.drawCircle(12f,17f,1.1f,fill)}
            "hill"->{p.moveTo(2.5f,19f);p.lineTo(9.5f,8f);p.lineTo(13.5f,12.5f);p.lineTo(16.5f,9.5f);p.lineTo(21.5f,19f);canvas.drawPath(p,paint);canvas.drawLine(2.5f,19f,21.5f,19f,paint)}
            // The coach: a steering wheel.
            "coach"->{canvas.drawCircle(12f,12f,8.5f,paint);canvas.drawCircle(12f,12f,2.2f,paint);canvas.drawLine(3.8f,10.5f,9.9f,11.4f,paint);canvas.drawLine(20.2f,10.5f,14.1f,11.4f,paint);canvas.drawLine(12f,14.2f,12f,20.4f,paint)}
            "settings"->{canvas.drawLine(4f,7f,20f,7f,paint);canvas.drawLine(4f,17f,20f,17f,paint);fill.color=paint.color;canvas.drawCircle(9f,7f,2.4f,fill);canvas.drawCircle(15f,17f,2.4f,fill)}
            "location"->{canvas.drawCircle(12f,10f,6f,paint);canvas.drawCircle(12f,10f,1.5f,paint);p.moveTo(7f,15f);p.lineTo(12f,21f);p.lineTo(17f,15f);canvas.drawPath(p,paint)}
            else->{canvas.drawCircle(12f,12f,7f,paint)}
        }
        canvas.restoreToCount(save)
    }
    override fun setAlpha(alpha:Int){paint.alpha=alpha;fill.alpha=alpha;invalidateSelf()}
    override fun setColorFilter(colorFilter:ColorFilter?){paint.colorFilter=colorFilter;fill.colorFilter=colorFilter;invalidateSelf()}
    @Deprecated("Deprecated in Java") override fun getOpacity()=PixelFormat.TRANSLUCENT
}
