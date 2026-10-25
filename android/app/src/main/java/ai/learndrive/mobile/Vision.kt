package ai.learndrive.mobile

import android.content.Context
import android.media.Image
import androidx.camera.core.ImageProxy
import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OnnxJavaType
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import ai.onnxruntime.TensorInfo
import ai.onnxruntime.OrtProvider
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
    val completedAt:Double=time+processingMs/1000,val frame:FrameContext?=null,
    val spatialReadiness:Int=SpatialReadiness.ORIGINAL_CALIBRATION_UNKNOWN,val semanticCaptureTime:Double?=null,
    val semanticCompletedAt:Double?=null,val semanticProcessingMs:Double?=null)

/** Cumulative steady-state diagnostics for one Vision instance, across source
 * changes. Construction, model hashing and startup benchmarks are excluded.
 * Frames count completed process calls; frameTotalMs also includes failed
 * attempts. Calls count attempted model-stage invocations; failures are separate. Detector
 * time wraps run/output-copy only. Spatial time wraps process (geometry, packing,
 * JNI and inference), excluding publication. spatialNativeCompletions counts
 * successful native processing returns, which require a completed model run.
 * A failed stage may fail before or after model work; its internal ORT attempt
 * count is unknown. Null last-stage time means that stage was skipped on the
 * latest attempted frame, not a zero-duration run. */
data class VisionPerformanceSnapshot(val instanceId:Long,val sourceId:Long=0,
    val lastCaptureTime:Double=Double.NEGATIVE_INFINITY,val publishedAt:Double=0.0,
    val frames:Long=0,val frameFailures:Long=0,val frameTotalMs:Double=0.0,
    val detectorCalls:Long=0,val detectorFailures:Long=0,val detectorTotalMs:Double=0.0,
    val spatialCalls:Long=0,val spatialFailures:Long=0,val spatialTotalMs:Double=0.0,
    val lastReadiness:Int=SpatialReadiness.ORIGINAL_CALIBRATION_UNKNOWN,
    val lastFrameMs:Double=0.0,val lastDetectorMs:Double?=null,val lastSpatialMs:Double?=null,
    val spatialNativeCompletions:Long=0)

class Vision(private val context:Context,private val core:NativeCore) : AutoCloseable {
    private val performanceInstanceId=android.os.SystemClock.elapsedRealtimeNanos()
    @Volatile var performanceSnapshot=VisionPerformanceSnapshot(performanceInstanceId)
        private set
    // Written only by the existing single inference executor. Readers consume
    // the immutable volatile snapshot rather than these in-progress counters.
    private var performanceFrames=0L
    private var performanceFrameFailures=0L
    private var performanceFrameTotalMs=0.0
    private var performanceDetectorCalls=0L
    private var performanceDetectorFailures=0L
    private var performanceDetectorTotalMs=0.0
    private var performanceSpatialCalls=0L
    private var performanceSpatialNativeCompletions=0L
    private var performanceSpatialFailures=0L
    private var performanceSpatialTotalMs=0.0
    private var performanceReadiness=SpatialReadiness.ORIGINAL_CALIBRATION_UNKNOWN
    private var performanceLastDetectorMs:Double?=null
    private var performanceLastSpatialMs:Double?=null
    private var sourceId=0L
    private var semanticTime=Double.NEGATIVE_INFINITY
    private var semanticResult:VisionResult?=null
    private var windowStarted=0.0
    private var windowFrames=0
    private var spatialActive=false
    private var frameSequence=0L
    private val environment=OrtEnvironment.getEnvironment()
    private val input=ByteBuffer.allocateDirect(3*320*320*4).order(ByteOrder.nativeOrder())
    private val tensor=OnnxTensor.createTensor(environment,input.asFloatBuffer(),longArrayOf(1,3,320,320))
    private inner class Model(name:String,private val outputs:List<String>) : AutoCloseable {
        val provider:String
        private val session:OrtSession
        val shapes:List<LongArray>
        val values:Array<FloatArray>
        init {
            val bytes=context.assets.open(name).use {it.readBytes()}
            var selected="CPU"
            session=runCatching {
                require(OrtEnvironment.getAvailableProviders().contains(OrtProvider.XNNPACK))
                OrtSession.SessionOptions().use { options ->
                    options.setIntraOpNumThreads(2);options.setInterOpNumThreads(1)
                    options.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
                    options.addXnnpack(emptyMap())
                    environment.createSession(bytes,options)
                }.also {selected="XNNPACK"}
            }.getOrElse {
                OrtSession.SessionOptions().use {options ->
                    options.setIntraOpNumThreads(2);options.setInterOpNumThreads(1)
                    options.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
                    options.addCPU(true)
                    environment.createSession(bytes,options)
                }
            }
            provider=selected
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
    private val spatial=try {SpatialVision(context,core)}catch(e:Exception){detector.close();tensor.close();throw e}
    val providerStatus="Supercombo ${spatial.providerStatus} · semantic detector ${detector.provider}"
    private val detectionShape=detector.shapes[0]
    private val channelFirst=detectionShape.size==3&&detectionShape[1]==84L
    private val count:Int
    init {
        try {
            require(detectionShape.size==3&&detectionShape[0]==1L&&(channelFirst||detectionShape[2]==84L)) {"Detector must output raw YOLOv8 COCO [1,84,N] or [1,N,84]"}
            count=(if(channelFirst)detectionShape[2] else detectionShape[1]).toInt()
        }catch(e:Exception){close();throw e}
    }
    @androidx.annotation.OptIn(androidx.camera.core.ExperimentalGetImage::class)
    fun frame(image:ImageProxy,id:Long,source:CaptureSource,onSemantic:(VisionResult)->Unit={}):VisionResult {
        val media=image.image?:throw IllegalArgumentException("Camera did not provide YUV image")
        val time=image.imageInfo.timestamp/1e9
        require(kotlin.math.abs(now()-time)<5) {"Camera timestamp is not on the elapsed-realtime clock"}
        return process(media,image.imageInfo.rotationDegrees,FrameContext(id,source,time,
            intrinsics=if(source==CaptureSource.CAMERA)SpatialVision.sensorIntrinsics?.takeIf {
                it.size==6&&it.all {v->v.isFinite()}&&it[0]>0&&it[1]>0&&it[4]>0&&it[5]>0
            }?.copyOf() else null,
            calibrationOrigin=if(source==CaptureSource.CAMERA)SpatialVision.calibrationOrigin else "unknown"),onSemantic=onSemantic)
    }
    fun process(image:Image,rotation:Int,originalFrame:FrameContext,fullRange:Boolean=false,onSemantic:(VisionResult)->Unit={}):VisionResult {
        val started=now()
        performanceReadiness=SpatialReadiness.ORIGINAL_CALIBRATION_UNKNOWN
        performanceLastDetectorMs=null;performanceLastSpatialMs=null
        try {
            return processFrame(image,rotation,originalFrame,fullRange,onSemantic).also {performanceFrames++}
        }catch(error:Throwable) {
            performanceFrameFailures++;throw error
        }finally {
            val publishedAt=now();val duration=(publishedAt-started)*1000
            performanceFrameTotalMs+=duration
            performanceSnapshot=VisionPerformanceSnapshot(performanceInstanceId,originalFrame.sourceId,
                originalFrame.captureTime,publishedAt,performanceFrames,performanceFrameFailures,performanceFrameTotalMs,
                performanceDetectorCalls,performanceDetectorFailures,performanceDetectorTotalMs,
                performanceSpatialCalls,performanceSpatialFailures,performanceSpatialTotalMs,
                performanceReadiness,duration,performanceLastDetectorMs,performanceLastSpatialMs,
                spatialNativeCompletions=performanceSpatialNativeCompletions)
        }
    }
    private fun processFrame(image:Image,rotation:Int,originalFrame:FrameContext,fullRange:Boolean,onSemantic:(VisionResult)->Unit):VisionResult {
        val time=originalFrame.captureTime
        core.requireSource(originalFrame.sourceId)
        if(sourceId!=originalFrame.sourceId) {
            spatial.reset();spatialActive=false;semanticTime=Double.NEGATIVE_INFINITY;semanticResult=null;sourceId=originalFrame.sourceId;frameSequence=0
        }
        val rect=image.cropRect
        val frame=originalFrame.copy(imageWidth=image.width,imageHeight=image.height,rotationDegrees=rotation,
            cropLeft=rect.left,cropTop=rect.top,cropWidth=rect.width(),cropHeight=rect.height(),pixelRange=if(fullRange)"full" else "video",
            pixelFormat=image.format,frameSequence=++frameSequence)
        val started=now()
        val up=core.cameraUp(time)
        val readiness=SpatialReadiness.evaluate(frame.source==CaptureSource.CAMERA,frame.intrinsics!=null,up[0],up[1],up[2])
        performanceReadiness=readiness
        val metric=readiness==SpatialReadiness.READY
        if(!metric) {
            core.spatialUnavailable(frame.sourceId,time)
            if(spatialActive){spatial.reset();spatialActive=false;semanticTime=Double.NEGATIVE_INFINITY;semanticResult=null}
        }
        // Publish classification before the expensive spatial model. Native rules
        // can consume the original capture timestamp while spatial inference runs.
        // Up to 8 Hz reduces the freshness gaps of the former 4 Hz schedule.
        if(time-semanticTime>=.125) {
            val semantic=semanticProcess(image,rotation,frame,fullRange).copy(spatialReadiness=readiness,semanticCaptureTime=time)
            semanticResult=semantic;semanticTime=time;onSemantic(semantic)
        }
        if(metric) {
            if(!spatialActive)spatial.reset()
            val spatialStarted=now();performanceSpatialCalls++
            try {
                spatial.process(image,rotation,frame,fullRange,up)
                performanceSpatialNativeCompletions++
            }catch(error:Throwable) {
                performanceSpatialFailures++;throw error
            }finally {
                val duration=(now()-spatialStarted)*1000
                performanceSpatialTotalMs+=duration;performanceLastSpatialMs=duration
            }
            spatial.publish(frame);spatialActive=true
        }
        core.requireSource(frame.sourceId)
        val last=semanticResult
        val completed=now();windowFrames++
        if(windowStarted==0.0)windowStarted=completed
        if(completed-windowStarted>=1) {
            val thermal=if(android.os.Build.VERSION.SDK_INT>=29)context.getSystemService(android.os.PowerManager::class.java).currentThermalStatus else 0
            android.util.Log.i("LearnDrivePerf","frames time=$completed fps=${windowFrames/(completed-windowStarted)} inference_ms=${(completed-started)*1000} thermal=$thermal")
            windowStarted=completed;windowFrames=0
        }
        val crop=image.cropRect;val swapped=rotation==90||rotation==270
        return VisionResult(time,last?.tti?:Double.POSITIVE_INFINITY,last?.confidence?:0.0,last?.inPath?:false,
            (now()-started)*1000,last?.detections?:emptyList(),if(swapped)crop.height() else crop.width(),
            if(swapped)crop.width() else crop.height(),RoadShape.from(core.roadShape()),now(),frame,readiness,last?.time,
            last?.completedAt,last?.processingMs)
    }
    private fun semanticProcess(image:Image,rotation:Int,frame:FrameContext,fullRange:Boolean=false):VisionResult {
        val time=frame.captureTime
        val started=now();val crop=image.cropRect;val planes=image.planes
        fun plane(i:Int):ByteBuffer {
            val p=planes[i];val x=if(i==0)crop.left else crop.left/2;val y=if(i==0)crop.top else crop.top/2
            return p.buffer.duplicate().apply {position(position()+y*p.rowStride+x*p.pixelStride)}.slice()
        }
        core.prepareFrame(frame.sourceId,plane(0),planes[0].rowStride,planes[0].pixelStride,plane(1),planes[1].rowStride,planes[1].pixelStride,
            plane(2),planes[2].rowStride,planes[2].pixelStride,crop.width(),crop.height(),rotation,320,input,0,1f,0,fullRange,true,1)
        val detectorStarted=now();performanceDetectorCalls++
        try {
            detector.run()
        }catch(error:Throwable) {
            performanceDetectorFailures++;throw error
        }finally {
            val duration=(now()-detectorStarted)*1000
            performanceDetectorTotalMs+=duration;performanceLastDetectorMs=duration
        }
        // ONNX YOLOv8 coordinates are pixels in the 320x320 model image.
        val hazard=core.spatialLabels(frame.sourceId,time,detector.values[0],count,channelFirst)
        val detections=core.boxes().toList().chunked(6).map {Detection(it[0],it[1],it[2],it[3],it[4],it[5].toInt())}
        val swapped=rotation==90||rotation==270
        val shape=RoadShape.from(core.roadShape());val completed=now()
        return VisionResult(time,hazard[0],hazard[1],hazard[2]!=0.0,(completed-started)*1000,detections,
            if(swapped)crop.height() else crop.width(),if(swapped)crop.width() else crop.height(),shape,completed)
    }
    override fun close() {spatial.close();detector.close();tensor.close()}
}
