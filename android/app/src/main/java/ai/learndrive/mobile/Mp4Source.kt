package ai.learndrive.mobile

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import java.util.concurrent.atomic.AtomicBoolean

class Mp4Source : AutoCloseable {
    private val stopped=AtomicBoolean()
    // MediaCodec owns decoded YUV planes; only the reusable input tensor is written.
    // PTS paces playback, while all fusion/rule timestamps use the phone clock.
    fun play(context:Context,uri:Uri,vision:Vision,sourceId:Long,result:(VisionResult)->Unit) {
        val extractor=MediaExtractor();var codec:MediaCodec?=null
        try {
            extractor.setDataSource(context,uri,null)
            val index=(0 until extractor.trackCount).firstOrNull {extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("video/")==true}
                ?:throw IllegalArgumentException("MP4 has no video track")
            extractor.selectTrack(index);val format=extractor.getTrackFormat(index)
            val rotation=if(format.containsKey(MediaFormat.KEY_ROTATION))format.getInteger(MediaFormat.KEY_ROTATION) else 0
            codec=MediaCodec.createDecoderByType(requireNotNull(format.getString(MediaFormat.KEY_MIME)))
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT,android.media.MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible)
            codec.configure(format,null,null,0);codec.start()
            var inputEnded=false;var outputEnded=false;var firstPts=-1L;val start=now();val info=MediaCodec.BufferInfo()
            while(!stopped.get()&&!outputEnded) {
                if(!inputEnded) {
                    val slot=codec.dequeueInputBuffer(1000)
                    if(slot>=0) {
                        val buffer=requireNotNull(codec.getInputBuffer(slot));val bytes=extractor.readSampleData(buffer,0)
                        if(bytes<0){codec.queueInputBuffer(slot,0,0,0,MediaCodec.BUFFER_FLAG_END_OF_STREAM);inputEnded=true}
                        else {codec.queueInputBuffer(slot,0,bytes,extractor.sampleTime,0);extractor.advance()}
                    }
                }
                val slot=codec.dequeueOutputBuffer(info,1000)
                if(slot>=0) {
                    try {
                        if(firstPts<0)firstPts=info.presentationTimeUs
                        val due=start+(info.presentationTimeUs-firstPts)/1e6
                        while(!stopped.get()&&now()<due)Thread.sleep(2)
                        if(!stopped.get()&&now()-due<.1&&info.size>0) {
                            val image=codec.getOutputImage(slot)?:throw IllegalStateException("Decoder cannot expose YUV; use another codec/video")
                            val outputFormat=codec.outputFormat
                            val full=outputFormat.containsKey(MediaFormat.KEY_COLOR_RANGE)&&outputFormat.getInteger(MediaFormat.KEY_COLOR_RANGE)==MediaFormat.COLOR_RANGE_FULL
                            val frame=FrameContext(sourceId,CaptureSource.RECORDING,due,info.presentationTimeUs/1e6,
                                pixelRangeOrigin=if(outputFormat.containsKey(MediaFormat.KEY_COLOR_RANGE))"decoder metadata" else "assumed video range")
                            image.use {result(vision.process(it,rotation,frame,full))}
                        }
                        outputEnded=info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM!=0
                    } finally {codec.releaseOutputBuffer(slot,false)}
                }
            }
        } finally {codec?.stop();codec?.release();extractor.release()}
    }
    override fun close() {stopped.set(true)}
}
