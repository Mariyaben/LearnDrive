package ai.learndrive.mobile

import android.content.Context
import android.media.Image
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

/** Owns native ORT/temporal state on the single inference executor. */
class SpatialVision(context:Context,private val core:NativeCore):AutoCloseable {
    companion object {
        @Volatile var sensorIntrinsics:DoubleArray?=null
        @Volatile var calibrationOrigin:String="unknown"
        init {System.loadLibrary("learndrive")}
    }
    private val output=ByteBuffer.allocateDirect(6472*4).order(ByteOrder.nativeOrder())
    private var handle:Long
    val providerStatus:String
    init {
        val expected="2365bae967cce21ce68707c30bf2981bb7081ee5c3e6a3dff793e660f23ff622"
        val model=File(context.filesDir,"supercombo-$expected.onnx")
        fun digest(file:File)=file.inputStream().use {stream ->
            val md=MessageDigest.getInstance("SHA-256");val chunk=ByteArray(65536)
            while(true){val n=stream.read(chunk);if(n<0)break;md.update(chunk,0,n)}
            md.digest().joinToString(""){"%02x".format(it)}
        }
        if(!model.exists()||digest(model)!=expected) {
            val pending=File(context.filesDir,"supercombo.download")
            context.assets.open("supercombo.onnx").use {src->pending.outputStream().use {src.copyTo(it)}}
            check(digest(pending)==expected){"Supercombo asset SHA-256 mismatch"}
            check(pending.renameTo(model)){"Could not install offline Supercombo asset"}
        }
        val backend=File(context.applicationInfo.nativeLibraryDir,"libQnnHtp.so")
        handle=create(model.absolutePath,if(backend.exists())backend.absolutePath else "").also {check(it!=0L)}
        providerStatus=provider(handle)
        android.util.Log.i("LearnDrivePerf","provider $providerStatus")
        val timing=core.benchmark()
        android.util.Log.i("LearnDrivePerf","decisions p50_us=${timing[0]} p95_us=${timing[1]} p99_us=${timing[2]} max_us=${timing[3]} mean_us=${timing[4]}")
    }
    fun process(image:Image,rotation:Int,frame:FrameContext,fullRange:Boolean,up:DoubleArray) {
        val time=frame.captureTime
        val crop=image.cropRect;val raw=frame.intrinsics
        // Intrinsics are scaled through the sensor's centered aspect-ratio crop.
        // Without optical metadata, fail visibly instead of inventing metric range.
        check(raw!=null){"Camera intrinsics unavailable: Supercombo requires rear-camera optical metadata"}
        val aspect=image.width.toDouble()/image.height
        val sensorAspect=raw[4]/raw[5]
        val sensorW=if(sensorAspect>aspect)raw[5]*aspect else raw[4]
        val sensorH=if(sensorAspect>aspect)raw[5] else raw[4]/aspect
        val scaleX=image.width/sensorW;val scaleY=image.height/sensorH
        var fx=raw[0]*scaleX;var fy=raw[1]*scaleY
        var cx=(raw[2]-(raw[4]-sensorW)/2)*scaleX-crop.left
        var cy=(raw[3]-(raw[5]-sensorH)/2)*scaleY-crop.top
        val w=if(rotation==90||rotation==270)crop.height() else crop.width()
        val h=if(rotation==90||rotation==270)crop.width() else crop.height()
        val oldCx=cx
        when(rotation){90->{cx=crop.height()-1-cy;cy=oldCx;val oldFx=fx;fx=fy;fy=oldFx}
            180->{cx=crop.width()-1-cx;cy=crop.height()-1-cy}
            270->{cx=cy;cy=crop.width()-1-oldCx;val oldFx=fx;fx=fy;fy=oldFx}}
        check(SpatialReadiness.evaluate(true,true,up[0],up[1],up[2])==SpatialReadiness.READY){"Fresh forward camera pose required"}
        val ux=up[0];val uy=up[1]
        val rx=when(rotation){90->uy;180->-ux;270->-uy;else->ux}
        val ry=when(rotation){90->-ux;180->-uy;270->ux;else->uy}
        val geometry=doubleArrayOf(fx,fy,cx,cy,w.toDouble(),h.toDouble(),rx,ry,up[2])
        core.frameGeometry(frame.sourceId,geometry)
        val planes=image.planes
        fun plane(i:Int):ByteBuffer {
            val p=planes[i];val x=if(i==0)crop.left else crop.left/2;val y=if(i==0)crop.top else crop.top/2
            return p.buffer.duplicate().apply{position(position()+y*p.rowStride+x*p.pixelStride)}.slice()
        }
        check(process(handle,plane(0),planes[0].rowStride,planes[0].pixelStride,plane(1),planes[1].rowStride,planes[1].pixelStride,
            plane(2),planes[2].rowStride,planes[2].pixelStride,crop.width(),crop.height(),rotation,fullRange,time,geometry,output))
    }
    fun publish(frame:FrameContext){check(core.spatialPublish(frame.sourceId,frame.captureTime,output)){"Supercombo result rejected"}}
    fun reset(){if(handle!=0L)reset(handle)}
    override fun close(){if(handle!=0L){destroy(handle);handle=0}}
    private external fun create(path:String,qnnBackend:String):Long
    private external fun destroy(handle:Long)
    private external fun reset(handle:Long)
    private external fun provider(handle:Long):String
    private external fun process(handle:Long,y:ByteBuffer,yr:Int,yp:Int,u:ByteBuffer,ur:Int,up:Int,v:ByteBuffer,vr:Int,vp:Int,
        width:Int,height:Int,rotation:Int,full:Boolean,time:Double,geometry:DoubleArray,output:ByteBuffer):Boolean
}
