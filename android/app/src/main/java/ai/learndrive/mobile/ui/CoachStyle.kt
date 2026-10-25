package ai.learndrive.mobile.ui

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.*
import android.graphics.drawable.*
import android.view.Gravity
import android.widget.Button
import android.widget.TextView
import kotlin.math.roundToInt

/** Shared native styling, local vector icons and system fonts. */
object CoachStyle {
    val ink=Color.rgb(13,14,16)
    val surface=Color.rgb(22,23,26)
    val raised=Color.rgb(34,36,41)
    val text=Color.rgb(244,245,247)
    val muted=Color.rgb(166,172,183)
    val accent=text
    val amber=Color.rgb(255,210,143)
    fun dp(context:Context,value:Int)=(value*context.resources.displayMetrics.density).roundToInt()
    fun panel(context:Context,color:Int=surface,radius:Int=10,stroke:Int=Color.rgb(48,51,58))=
        GradientDrawable().apply {setColor(color);cornerRadius=dp(context,radius).toFloat();setStroke(dp(context,1),stroke)}
    fun label(context:Context,value:String,size:Float=14f,color:Int=text)=TextView(context).apply {
        text=value;textSize=size;setTextColor(color);typeface=Typeface.create("sans-serif",Typeface.NORMAL);includeFontPadding=false
    }
    fun styleButton(button:Button,primary:Boolean=false)=button.apply {
        isAllCaps=false;textSize=14f;typeface=Typeface.create("sans-serif-medium",Typeface.NORMAL)
        gravity=Gravity.CENTER;minimumHeight=dp(context,48);minHeight=dp(context,48);minimumWidth=0;minWidth=0
        setPadding(dp(context,16),0,dp(context,16),0);stateListAnimator=null
        setTextColor(ColorStateList(arrayOf(intArrayOf(-android.R.attr.state_enabled),intArrayOf()),
            intArrayOf(Color.rgb(127,133,145),if(primary)ink else CoachStyle.text)))
        val states=StateListDrawable().apply {
            addState(intArrayOf(-android.R.attr.state_enabled),panel(context,if(primary)Color.rgb(48,51,58) else surface,8))
            addState(intArrayOf(android.R.attr.state_selected),panel(context,if(primary)CoachStyle.text else raised,8))
            addState(intArrayOf(),panel(context,if(primary)CoachStyle.text else surface,8))
        }
        background=RippleDrawable(ColorStateList.valueOf(Color.argb(26,100,105,115)),states,null)
    }
    fun styleTab(button:Button)=button.apply {
        styleButton(this)
        setTextColor(ColorStateList(arrayOf(intArrayOf(android.R.attr.state_selected),intArrayOf()),intArrayOf(CoachStyle.text,muted)))
        fun underline(color:Int)=LayerDrawable(arrayOf(ColorDrawable(surface),ColorDrawable(color))).apply {
            setLayerHeight(1,dp(context,2));setLayerGravity(1,Gravity.BOTTOM)
        }
        background=RippleDrawable(ColorStateList.valueOf(Color.argb(20,0,0,0)),StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_selected),underline(CoachStyle.text));addState(intArrayOf(),underline(Color.rgb(48,51,58)))
        },null)
    }
    fun button(context:Context,value:String,primary:Boolean=false)=styleButton(Button(context).apply {text=value},primary)
    fun icon(context:Context,kind:String,color:Int=text)=CoachIcon(context,kind,color)
}

class CoachIcon(private val context:Context,private val kind:String,color:Int):Drawable() {
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG).apply {this.color=color;style=Paint.Style.STROKE;strokeWidth=1.8f;strokeCap=Paint.Cap.ROUND;strokeJoin=Paint.Join.ROUND}
    override fun getIntrinsicWidth()=CoachStyle.dp(context,24)
    override fun getIntrinsicHeight()=CoachStyle.dp(context,24)
    override fun draw(canvas:Canvas) {
        val save=canvas.save();canvas.translate(bounds.left.toFloat(),bounds.top.toFloat());canvas.scale(bounds.width()/24f,bounds.height()/24f)
        val p=Path()
        when(kind) {
            "search"->{canvas.drawCircle(10.5f,10.5f,5.7f,paint);canvas.drawLine(15f,15f,20f,20f,paint)}
            "close"->{canvas.drawLine(6f,6f,18f,18f,paint);canvas.drawLine(18f,6f,6f,18f,paint)}
            "brand"->{p.moveTo(6f,20f);p.lineTo(9f,4f);p.moveTo(18f,20f);p.lineTo(15f,4f);p.moveTo(12f,6f);p.lineTo(12f,10f);p.moveTo(12f,14f);p.lineTo(12f,18f);canvas.drawPath(p,paint)}
            "check"->{p.moveTo(5f,12f);p.lineTo(10f,17f);p.lineTo(19f,7f);canvas.drawPath(p,paint)}
            "camera"->{canvas.drawRoundRect(3f,6f,21f,19f,3f,3f,paint);canvas.drawCircle(12f,12.5f,3.3f,paint);canvas.drawLine(7f,4f,12f,4f,paint)}
            "voice"->{p.moveTo(4f,10f);p.lineTo(8f,10f);p.lineTo(12f,6f);p.lineTo(12f,18f);p.lineTo(8f,14f);p.lineTo(4f,14f);p.close();canvas.drawPath(p,paint);canvas.drawArc(8f,5f,23f,19f,-50f,100f,false,paint);canvas.drawArc(11f,8f,20f,16f,-50f,100f,false,paint)}
            "left","right"->{if(kind=="left"){canvas.translate(24f,0f);canvas.scale(-1f,1f)};p.moveTo(6f,20f);p.lineTo(6f,12f);p.quadTo(6f,8f,10f,8f);p.lineTo(20f,8f);p.moveTo(15f,3f);p.lineTo(20f,8f);p.lineTo(15f,13f);canvas.drawPath(p,paint)}
            "straight"->{canvas.drawLine(12f,21f,12f,3f,paint);p.moveTo(6f,9f);p.lineTo(12f,3f);p.lineTo(18f,9f);canvas.drawPath(p,paint)}
            "coach"->{p.moveTo(3f,12f);p.lineTo(7f,12f);p.lineTo(10f,5f);p.lineTo(14f,19f);p.lineTo(17f,12f);p.lineTo(21f,12f);canvas.drawPath(p,paint)}
            else->{canvas.drawCircle(12f,10f,6f,paint);canvas.drawCircle(12f,10f,1.5f,paint);p.moveTo(7f,15f);p.lineTo(12f,21f);p.lineTo(17f,15f);canvas.drawPath(p,paint)}
        }
        canvas.restoreToCount(save)
    }
    override fun setAlpha(alpha:Int){paint.alpha=alpha;invalidateSelf()}
    override fun setColorFilter(colorFilter:ColorFilter?){paint.colorFilter=colorFilter;invalidateSelf()}
    @Deprecated("Deprecated in Java") override fun getOpacity()=PixelFormat.TRANSLUCENT
}
