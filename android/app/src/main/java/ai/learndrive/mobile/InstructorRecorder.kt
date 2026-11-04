package ai.learndrive.mobile

import android.content.Context
import android.media.MediaRecorder
import android.os.Build
import java.io.File
import java.io.IOException

/**
 * Opt-in recording of a supervised lesson's spoken instructions, for the
 * instructor benchmark (tools/instructor_benchmark.py). It runs only together
 * with local diagnostics, so its start time is on the diagnostics journal clock,
 * and it writes one AAC file to app-private storage. Nothing is uploaded; the
 * file is exported or deleted together with the diagnostics.
 *
 * The microphone also hears the app's own voice and anything else said in the
 * car. Everyone in the car must agree before recording starts.
 */
class InstructorRecorder(private val context:Context,val file:File) {
    @Volatile private var recorder:MediaRecorder?=null
    @Volatile private var limitReached=false
    val active get()=recorder!=null
    val sizeLimitReached get()=limitReached

    /** Start time: the midpoint of MediaRecorder.start() on [clock], and half its duration as uncertainty. */
    data class Start(val time:Double,val uncertaintySeconds:Double)

    fun start(clock:()->Double):Start {
        check(recorder==null){"Instructor audio is already recording"}
        file.parentFile?.let {if(!it.isDirectory&&!it.mkdirs())throw IOException("Cannot create lesson directory")}
        if(file.exists()&&!file.delete())throw IOException("Cannot replace earlier instructor audio")
        val r=if(Build.VERSION.SDK_INT>=31)MediaRecorder(context) else @Suppress("DEPRECATION") MediaRecorder()
        try {
            r.setAudioSource(MediaRecorder.AudioSource.MIC)
            r.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4)
            r.setAudioEncoder(MediaRecorder.AudioEncoder.AAC)
            r.setAudioChannels(1);r.setAudioSamplingRate(16000);r.setAudioEncodingBitRate(32000)
            r.setMaxFileSize(MAX_BYTES) // about 3.5 hours at 32 kbit/s
            r.setOnInfoListener {_,what,_->if(what==MediaRecorder.MEDIA_RECORDER_INFO_MAX_FILESIZE_REACHED)limitReached=true}
            r.setOutputFile(file.absolutePath)
            r.prepare()
            limitReached=false
            val before=clock();r.start();val after=clock()
            recorder=r
            return Start((before+after)/2,(after-before)/2)
        }catch(e:Exception){r.release();file.delete();throw e}
    }

    /** Stops and keeps the file. Returns false when nothing usable was recorded (the file is removed). */
    fun stop():Boolean {
        val r=recorder?:return false;recorder=null
        // After the size limit the recorder has already stopped itself.
        val ok=limitReached||runCatching {r.stop()}.isSuccess
        r.release()
        if(!ok)file.delete()
        return ok&&file.isFile
    }

    companion object {const val MAX_BYTES=48L*1024*1024}
}
