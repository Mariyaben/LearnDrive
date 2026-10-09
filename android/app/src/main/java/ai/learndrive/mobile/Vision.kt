package ai.learndrive.mobile

import android.content.Context
import android.media.Image
import androidx.camera.core.ImageProxy
import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OnnxJavaType
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import ai.onnxruntime.TensorInfo
import java.nio.ByteBuffer
import java.nio.ByteOrder
import android.graphics.PointF

data class Detection(val x:Float,val y:Float,val width:Float,val height:Float,val score:Float,val category:Int)
data class RoadShape(val valid:Boolean,val bend:Int,val points:List<PointF>) {
    val status get()=if(!valid)"road shape unclear" else when(bend){-1->"left curve";1->"right curve";else->"straight road"}
    companion object {
        fun from(v:DoubleArray)=RoadShape(v.size==13&&v[1]!=0.0,if(v.size==13)v[2].toInt() else 0,
            if(v.size==13&&v[1]!=0.0)(3..11 step 2).map {PointF(v[it].toFloat(),v[it+1].toFloat())} else emptyList())
    }
}
data class VisionResult(val time:Double,val tti:Double,val confidence:Double,val inPath:Boolean,
    val processingMs:Double,val detections:List<Detection>,val width:Int,val height:Int,val road:RoadShape?=null,
    val completedAt:Double=time+processingMs/1000)

class Vision(private val context:Context,private val core:NativeCore) : AutoCloseable {
    private val environment=OrtEnvironment.getEnvironment()
    private val input=ByteBuffer.allocateDirect(3*320*320*4).order(ByteOrder.nativeOrder())
    private val tensor=OnnxTensor.createTensor(environment,input.asFloatBuffer(),longArrayOf(1,3,320,320))
    private inner class Model(name:String,private val outputs:List<String>) : AutoCloseable {
        private val session=OrtSession.SessionOptions().use { options ->
            options.setIntraOpNumThreads(2);options.setInterOpNumThreads(1)
            environment.createSession(context.assets.open(name).use {it.readBytes()},options)
        }
        val shapes:List<LongArray>
        val values:Array<FloatArray>
        init {
            try {
                val info=session.inputInfo["images"]?.info as? TensorInfo
                require(session.inputNames==setOf("images")&&info?.type==OnnxJavaType.FLOAT&&
                    info.shape.contentEquals(longArrayOf(1,3,320,320))) {"$name requires Float32 RGB [1,3,320,320]"}
                shapes=outputs.map { output ->
                    val shape=session.outputInfo[output]?.info as? TensorInfo
                    require(shape?.type==OnnxJavaType.FLOAT&&shape.shape.all {it>0}) {"$name: missing Float32 $output"}
                    shape.shape
                }
                values=Array(shapes.size) {i->FloatArray(shapes[i].fold(1L) {a,b->a*b}.toInt())}
            }catch(e:Exception){session.close();throw e}
        }
        fun run() {
            session.run(mapOf("images" to tensor)).use {result ->
                for(i in outputs.indices) {
                    val output=result.get(outputs[i]).orElseThrow {IllegalArgumentException("Missing ${outputs[i]}")} as OnnxTensor
                    output.floatBuffer.get(values[i])
                }
            }
        }
        override fun close()=session.close()
    }
    private val detector=try {Model("detector.onnx",listOf("output0"))}catch(e:Exception){tensor.close();throw e}
    private val road=try {Model("road.onnx",listOf("drivable","lanes"))}catch(e:Exception){detector.close();tensor.close();throw e}
    private val detectionShape=detector.shapes[0]
    private val channelFirst=detectionShape.size==3&&detectionShape[1]==84L
    private val count:Int
    private val maskWidth:Int
    private val maskHeight:Int
    init {
        try {
            require(detectionShape.size==3&&detectionShape[0]==1L&&(channelFirst||detectionShape[2]==84L)) {"Detector must output raw YOLOv8 COCO [1,84,N] or [1,N,84]"}
            count=(if(channelFirst)detectionShape[2] else detectionShape[1]).toInt()
            val shape=road.shapes[0]
            require(shape.size==4&&shape[0]==1L&&shape[1]==2L&&shape.contentEquals(road.shapes[1])) {"Road outputs must be NCHW [1,2,H,W] logits"}
            maskHeight=shape[2].toInt();maskWidth=shape[3].toInt()
        }catch(e:Exception){close();throw e}
    }
    @androidx.annotation.OptIn(androidx.camera.core.ExperimentalGetImage::class)
    fun frame(image:ImageProxy):VisionResult {
        val media=image.image?:throw IllegalArgumentException("Camera did not provide YUV image")
        val time=image.imageInfo.timestamp/1e9
        require(kotlin.math.abs(now()-time)<5) {"Camera timestamp is not on the elapsed-realtime clock"}
        return process(media,image.imageInfo.rotationDegrees,time)
    }
    // Both pretrained networks use the same letterboxed RGB 0..1 tensor.
    fun process(image:Image,rotation:Int,time:Double,fullRange:Boolean=false):VisionResult {
        val started=now();val crop=image.cropRect;val planes=image.planes
        fun plane(i:Int):ByteBuffer {
            val p=planes[i];val x=if(i==0)crop.left else crop.left/2;val y=if(i==0)crop.top else crop.top/2
            return p.buffer.duplicate().apply {position(position()+y*p.rowStride+x*p.pixelStride)}.slice()
        }
        core.prepare(plane(0),planes[0].rowStride,planes[0].pixelStride,plane(1),planes[1].rowStride,planes[1].pixelStride,
            plane(2),planes[2].rowStride,planes[2].pixelStride,crop.width(),crop.height(),rotation,320,input,0,1f,0,fullRange,true,1)
        detector.run();road.run()
        // ONNX YOLOv8 coordinates are pixels in the 320x320 model image.
        val hazard=core.vision(time,detector.values[0],84,count,channelFirst,false,road.values[0],road.values[1],maskWidth,maskHeight)
        val detections=core.boxes().toList().chunked(6).map {Detection(it[0],it[1],it[2],it[3],it[4],it[5].toInt())}
        val swapped=rotation==90||rotation==270
        val shape=RoadShape.from(core.roadShape());val completed=now()
        return VisionResult(time,hazard[0],hazard[1],hazard[2]!=0.0,(completed-started)*1000,detections,
            if(swapped)crop.height() else crop.width(),if(swapped)crop.width() else crop.height(),shape,completed)
    }
    override fun close() {detector.close();road.close();tensor.close()}
}
