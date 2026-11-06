package ai.learndrive.mobile

import android.app.KeyguardManager
import android.media.AudioManager
import android.media.AudioTrack
import android.os.SystemClock
import android.view.WindowManager
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Stationary PCM output smoke. Playback counters cannot prove human audibility. */
@RunWith(AndroidJUnit4::class)
class AudioOutputSmokeTest {
    private fun field(name:String)=MainActivity::class.java.getDeclaredField(name).apply {isAccessible=true}
    private fun audioField(name:String)=AudioWorker::class.java.getDeclaredField(name).apply {isAccessible=true}

    @Test fun productionVoiceObtainsFocusAndPlaysTheBundledPhrase() {
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        ActivityScenario.launch(MainActivity::class.java).use {scenario->
            lateinit var audio:AudioWorker
            val deadline=SystemClock.elapsedRealtime()+15_000
            do {
                scenario.onActivity {activity->
                    assertFalse("Unlock phone for audio output check",activity.getSystemService(KeyguardManager::class.java).isKeyguardLocked)
                    assertFalse("Run stationary",MainActivity::class.java.getDeclaredMethod("knownMoving").apply {isAccessible=true}.invoke(activity) as Boolean)
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    audio=field("audio").get(activity) as AudioWorker
                }
                if(!audio.ready)Thread.sleep(50)
            }while(!audio.ready&&SystemClock.elapsedRealtime()<deadline)
            assertTrue("Production PCM output must initialize",audio.ready)
            val player=audioField("track").get(audio) as AudioTrack
            val duration=audio.durationSeconds(18)
            var accepted=false
            scenario.onActivity {accepted=audio.submit(18)}
            assertTrue("Audio focus and production submission must succeed",accepted)
            var maxFrames=0;var routeType:Int?=null
            val finish=SystemClock.elapsedRealtime()+((duration+3)*1000).toLong()
            do {
                maxFrames=maxOf(maxFrames,player.playbackHeadPosition)
                player.routedDevice?.let {routeType=it.type}
                val active=audioField("active").getInt(audio)
                val pending=audioField("pending").getInt(audio)
                if(maxFrames>0&&active==0&&pending==0)break
                Thread.sleep(10)
            }while(SystemClock.elapsedRealtime()<finish)
            assertTrue("PCM playback must progress through the phrase",maxFrames>=(duration-.25)*player.sampleRate)
            assertEquals("Voice must finish without getting stuck",0,audioField("active").getInt(audio))
            assertEquals(0,audioField("pending").getInt(audio))
            scenario.onActivity {activity->
                assertFalse("Output must not report an error",(field("audioStatus").get(activity) as String).startsWith("Audio output:"))
            }
            val manager=context.getSystemService(AudioManager::class.java)
            File(context.filesDir,"audio-output-smoke.json").writeText(JSONObject()
                .put("version",BuildConfig.VERSION_NAME).put("prompt",18).put("duration_seconds",duration)
                .put("max_played_frames_observed",maxFrames).put("sample_rate",player.sampleRate)
                .put("routed_device_type",routeType?:JSONObject.NULL)
                .put("music_stream_volume",manager.getStreamVolume(AudioManager.STREAM_MUSIC))
                .put("playback_completed",true).put("human_audibility_confirmed",false)
                .put("road_audio_timing_qualified",false).toString(2))
        }
    }
}
