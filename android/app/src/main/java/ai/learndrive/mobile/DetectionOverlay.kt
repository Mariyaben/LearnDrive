package ai.learndrive.mobile

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.view.View

class DetectionOverlay(context:Context):View(context) {
    var result:VisionResult?=null
        set(value){field=value;invalidate()}
    private val boxes=Paint(Paint.ANTI_ALIAS_FLAG).apply {color=Color.rgb(64,240,184);style=Paint.Style.STROKE;strokeWidth=2*resources.displayMetrics.density}
    private val labels=Paint(Paint.ANTI_ALIAS_FLAG).apply {color=Color.WHITE;textSize=13*resources.displayMetrics.scaledDensity;setShadowLayer(3f,1f,1f,Color.BLACK)}
    private val names=mapOf(0 to "Person",1 to "Bicycle",2 to "Car",3 to "Motorcycle",5 to "Bus",7 to "Truck")
    private val roadPaint=Paint(Paint.ANTI_ALIAS_FLAG).apply {color=Color.rgb(255,206,86);style=Paint.Style.STROKE;strokeWidth=3*resources.displayMetrics.density}
    override fun onDraw(canvas:Canvas) {
        super.onDraw(canvas)
        val frame=result?:return
        val scale=minOf(width.toFloat()/frame.width,height.toFloat()/frame.height)
        val w=frame.width*scale;val h=frame.height*scale;val dx=(width-w)/2;val dy=(height-h)/2
        frame.road?.takeIf {it.valid&&it.points.isNotEmpty()}?.let {road ->
            val path=Path();road.points.forEachIndexed {i,p->if(i==0)path.moveTo(dx+p.x*w,dy+p.y*h) else path.lineTo(dx+p.x*w,dy+p.y*h)}
            canvas.drawPath(path,roadPaint)
            canvas.drawText(road.status,dx+8,dy+labels.textSize+8,labels)
        }
        for(box in frame.detections) {
            val left=dx+box.x*w;val top=dy+box.y*h
            canvas.drawRect(left,top,left+box.width*w,top+box.height*h,boxes)
            canvas.drawText("${names[box.category]?:"Object"} ${(box.score*100).toInt()}%",left,(top-labels.textSize*.2f).coerceAtLeast(labels.textSize),labels)
        }
    }
}
