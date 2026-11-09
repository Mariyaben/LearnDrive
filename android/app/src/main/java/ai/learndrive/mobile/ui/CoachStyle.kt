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
 * "Learner plate on asphalt": road-black surfaces, road-paint white text and one signal-amber
 * accent (the L-plate / indicator colour). Green only means ready, red only means act now.
 * Condensed bold type, like road signs, for anything read at a glance while driving.
 * Local vector icons and system fonts only.
 */
object CoachStyle {
    val ink=Color.rgb(11,15,14)          // asphalt
    val surface=Color.rgb(20,26,24)
    val raised=Color.rgb(30,38,35)
    val line=Color.rgb(44,54,50)
    val text=Color.rgb(242,239,230)      // road paint
    val muted=Color.rgb(154,165,159)
    val amber=Color.rgb(255,182,39)      // signal amber
    val accent=amber
    val go=Color.rgb(74,222,154)
    val stop=Color.rgb(255,93,79)
    val display:Typeface=Typeface.create("sans-serif-condensed",Typeface.BOLD)
    val medium:Typeface=Typeface.create("sans-serif-medium",Typeface.NORMAL)
    fun dp(context:Context,value:Int)=(value*context.resources.displayMetrics.density).roundToInt()
    fun panel(context:Context,color:Int=surface,radius:Int=16,stroke:Int=line)=
        GradientDrawable().apply {setColor(color);cornerRadius=dp(context,radius).toFloat();if(stroke!=Color.TRANSPARENT)setStroke(dp(context,1),stroke)}
    /** A card with a thick amber edge on the left: the coach's voice. */
    fun voiceCard(context:Context,edge:Int=amber)=LayerDrawable(arrayOf(panel(context,raised,16,Color.TRANSPARENT),
        GradientDrawable().apply {setColor(edge);cornerRadii=floatArrayOf(dp(context,16).toFloat(),dp(context,16).toFloat(),0f,0f,0f,0f,dp(context,16).toFloat(),dp(context,16).toFloat())})).apply {
        setLayerWidth(1,dp(context,6));setLayerGravity(1,Gravity.START or Gravity.FILL_VERTICAL)
    }
    fun label(context:Context,value:String,size:Float=14f,color:Int=text)=TextView(context).apply {
        text=value;textSize=size;setTextColor(color);typeface=Typeface.create("sans-serif",Typeface.NORMAL);includeFontPadding=false
    }
    /** Small uppercase caption above a value. */
    fun eyebrow(context:Context,value:String,color:Int=muted)=label(context,value,10.5f,color).apply {letterSpacing=.12f;typeface=medium;isAllCaps=true}
    fun styleButton(button:Button,primary:Boolean=false)=button.apply {
        isAllCaps=false;textSize=15f;typeface=if(primary)CoachStyle.display else CoachStyle.medium;letterSpacing=if(primary).02f else 0f
        gravity=Gravity.CENTER;minimumHeight=dp(context,52);minHeight=dp(context,52);minimumWidth=0;minWidth=0
        setPadding(dp(context,18),0,dp(context,18),0);stateListAnimator=null
        setTextColor(ColorStateList(arrayOf(intArrayOf(-android.R.attr.state_enabled),intArrayOf()),
            intArrayOf(Color.rgb(110,120,115),if(primary)ink else CoachStyle.text)))
        val states=StateListDrawable().apply {
            addState(intArrayOf(-android.R.attr.state_enabled),panel(context,if(primary)line else surface,14,if(primary)Color.TRANSPARENT else line))
            addState(intArrayOf(android.R.attr.state_selected),panel(context,if(primary)amber else raised,14,if(primary)Color.TRANSPARENT else amber))
            addState(intArrayOf(),panel(context,if(primary)amber else raised,14,if(primary)Color.TRANSPARENT else line))
        }
        background=RippleDrawable(ColorStateList.valueOf(if(primary)Color.argb(60,0,0,0) else Color.argb(40,255,182,39)),states,null)
    }
    /** Segmented-control tab: the selected one sits on amber. */
    fun styleTab(button:Button)=button.apply {
        styleButton(this);typeface=medium
        setTextColor(ColorStateList(arrayOf(intArrayOf(android.R.attr.state_selected),intArrayOf()),intArrayOf(ink,muted)))
        background=RippleDrawable(ColorStateList.valueOf(Color.argb(30,255,182,39)),StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_selected),panel(context,amber,12,Color.TRANSPARENT))
            addState(intArrayOf(),panel(context,surface,12,Color.TRANSPARENT))
        },null)
    }
    fun button(context:Context,value:String,primary:Boolean=false)=styleButton(Button(context).apply {text=value},primary)
    fun icon(context:Context,kind:String,color:Int=text)=CoachIcon(context,kind,color)
}

class CoachIcon(private val context:Context,private val kind:String,color:Int):Drawable() {
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG).apply {this.color=color;style=Paint.Style.STROKE;strokeWidth=2.1f;strokeCap=Paint.Cap.ROUND;strokeJoin=Paint.Join.ROUND}
    private val fill=Paint(Paint.ANTI_ALIAS_FLAG).apply {this.color=color;style=Paint.Style.FILL}
    override fun getIntrinsicWidth()=CoachStyle.dp(context,24)
    override fun getIntrinsicHeight()=CoachStyle.dp(context,24)
    override fun draw(canvas:Canvas) {
        val save=canvas.save();canvas.translate(bounds.left.toFloat(),bounds.top.toFloat());canvas.scale(bounds.width()/24f,bounds.height()/24f)
        val p=Path()
        when(kind) {
            "search"->{canvas.drawCircle(10.5f,10.5f,5.7f,paint);canvas.drawLine(15f,15f,20f,20f,paint)}
            "close"->{canvas.drawLine(6f,6f,18f,18f,paint);canvas.drawLine(18f,6f,6f,18f,paint)}
            // The learner plate: an amber tile with a bold road-paint "L".
            "brand"->{fill.color=CoachStyle.amber;canvas.drawRoundRect(2f,2f,22f,22f,5f,5f,fill)
                val l=Paint(Paint.ANTI_ALIAS_FLAG).apply {color=CoachStyle.ink;style=Paint.Style.STROKE;strokeWidth=3.4f;strokeCap=Paint.Cap.SQUARE}
                p.moveTo(9f,6.5f);p.lineTo(9f,17f);p.lineTo(16.5f,17f);canvas.drawPath(p,l)}
            "check"->{p.moveTo(5f,12f);p.lineTo(10f,17f);p.lineTo(19f,7f);canvas.drawPath(p,paint)}
            "camera"->{canvas.drawRoundRect(3f,6f,21f,19f,3f,3f,paint);canvas.drawCircle(12f,12.5f,3.3f,paint);canvas.drawLine(7f,4f,12f,4f,paint)}
            "voice"->{p.moveTo(4f,10f);p.lineTo(8f,10f);p.lineTo(12f,6f);p.lineTo(12f,18f);p.lineTo(8f,14f);p.lineTo(4f,14f);p.close();canvas.drawPath(p,paint);canvas.drawArc(8f,5f,23f,19f,-50f,100f,false,paint);canvas.drawArc(11f,8f,20f,16f,-50f,100f,false,paint)}
            "left","right"->{if(kind=="left"){canvas.translate(24f,0f);canvas.scale(-1f,1f)};p.moveTo(6f,20f);p.lineTo(6f,12f);p.quadTo(6f,8f,10f,8f);p.lineTo(20f,8f);p.moveTo(15f,3f);p.lineTo(20f,8f);p.lineTo(15f,13f);canvas.drawPath(p,paint)}
            "straight"->{canvas.drawLine(12f,21f,12f,3f,paint);p.moveTo(6f,9f);p.lineTo(12f,3f);p.lineTo(18f,9f);canvas.drawPath(p,paint)}
            // Hills: the road climbing, a crest, and the descent.
            "hill"->{p.moveTo(2.5f,19f);p.lineTo(9.5f,8f);p.lineTo(13.5f,12.5f);p.lineTo(16.5f,9.5f);p.lineTo(21.5f,19f);canvas.drawPath(p,paint);canvas.drawLine(2.5f,19f,21.5f,19f,paint)}
            // The coach: a steering wheel.
            "coach"->{canvas.drawCircle(12f,12f,8.5f,paint);canvas.drawCircle(12f,12f,2.2f,paint);canvas.drawLine(3.8f,10.5f,9.9f,11.4f,paint);canvas.drawLine(20.2f,10.5f,14.1f,11.4f,paint);canvas.drawLine(12f,14.2f,12f,20.4f,paint)}
            "settings"->{canvas.drawLine(4f,7f,20f,7f,paint);canvas.drawLine(4f,17f,20f,17f,paint);fill.color=paint.color;canvas.drawCircle(9f,7f,2.4f,fill);canvas.drawCircle(15f,17f,2.4f,fill)}
            else->{canvas.drawCircle(12f,10f,6f,paint);canvas.drawCircle(12f,10f,1.5f,paint);p.moveTo(7f,15f);p.lineTo(12f,21f);p.lineTo(17f,15f);canvas.drawPath(p,paint)}
        }
        canvas.restoreToCount(save)
    }
    override fun setAlpha(alpha:Int){paint.alpha=alpha;fill.alpha=alpha;invalidateSelf()}
    override fun setColorFilter(colorFilter:ColorFilter?){paint.colorFilter=colorFilter;invalidateSelf()}
    @Deprecated("Deprecated in Java") override fun getOpacity()=PixelFormat.TRANSLUCENT
}
