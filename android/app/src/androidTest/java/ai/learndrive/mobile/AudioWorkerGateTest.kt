package ai.learndrive.mobile

import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutorService
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference

/** Actual cached PCM worker, without starting a spoken clip. Native ticket
 * freshness itself is covered by the native acceptance/onset contract tests. */
class AudioWorkerGateTest {
    private fun field(name:String)=AudioWorker::class.java.getDeclaredField(name).apply {isAccessible=true}
    @Test fun cancellationCannotBeFollowedByAnOldPcmWrite() {
        val audio=AudioWorker(InstrumentationRegistry.getInstrumentation().targetContext,{_,_->true},55) {}
        val write=AudioWorker::class.java.getDeclaredMethod("writeCurrent",Int::class.javaPrimitiveType,Function0::class.java).apply {isAccessible=true}
        val generation=field("generation").get(audio) as AtomicInteger
        val entered=CountDownLatch(1);val release=CountDownLatch(1);val attemptedCancel=CountDownLatch(1)
        val cancelled=CountDownLatch(1);val writes=AtomicInteger();val failure=AtomicReference<Throwable>()
        val token=generation.get()
        val writer=Thread {
            try {write.invoke(audio,token,{entered.countDown();release.await(2,TimeUnit.SECONDS);writes.incrementAndGet();0})}
            catch(e:Throwable){failure.set(e)}
        }
        val canceller=Thread {attemptedCancel.countDown();audio.cancel();cancelled.countDown()}
        try {
            writer.start();assertTrue(entered.await(2,TimeUnit.SECONDS))
            canceller.start();assertTrue(attemptedCancel.await(2,TimeUnit.SECONDS))
            assertFalse("Cancel/flush must serialize after an in-flight nonblocking write",cancelled.await(50,TimeUnit.MILLISECONDS))
            release.countDown();writer.join(2000);canceller.join(2000)
            assertTrue(cancelled.await(1,TimeUnit.SECONDS));assertNull(failure.get());assertEquals(1,writes.get())
            assertNull("Cancelled worker cannot append bytes after the flush",write.invoke(audio,token,{writes.incrementAndGet();0}))
            assertEquals(1,writes.get())
        }finally {release.countDown();writer.join(2000);if(canceller.isAlive)canceller.join(2000);audio.close()}
    }
    @Test fun muteCancelsQueuedVoiceAndOnsetGateNeverHoldsAudioMonitor() {
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val audio=AudioWorker(context,{_,_->true},55) {}
        val release=CountDownLatch(1)
        try {
            val deadline=SystemClock.elapsedRealtime()+15_000
            while(!audio.ready&&SystemClock.elapsedRealtime()<deadline)Thread.sleep(20)
            assertTrue("Cached production PCM must initialize",audio.ready)
            val entered=CountDownLatch(1)
            (field("worker").get(audio) as ExecutorService).execute {entered.countDown();release.await(5,TimeUnit.SECONDS)}
            assertTrue(entered.await(2,TimeUnit.SECONDS))
            val attempted=AtomicInteger()
            assertTrue(audio.submit(2) {play->attempted.incrementAndGet();play {true}})
            audio.setVoiceEnabled(false)
            assertEquals(0,field("pending").getInt(audio));assertEquals(0,field("active").getInt(audio))
            assertFalse("Muted Test voice/native dispatch cannot submit",audio.submit(2))
            release.countDown()
            val checked=CountDownLatch(1)
            (field("worker").get(audio) as ExecutorService).execute {checked.countDown()}
            assertTrue(checked.await(2,TimeUnit.SECONDS));assertEquals(0,attempted.get())
            audio.setVoiceEnabled(true)
            val onsetChecked=CountDownLatch(1)
            var heldMonitor=true
            var boundaryMonitor=false
            assertTrue(audio.submit(2) {play ->
                heldMonitor=Thread.holdsLock(audio)
                play {boundaryMonitor=Thread.holdsLock(audio);onsetChecked.countDown();false}
            })
            assertTrue(onsetChecked.await(2,TimeUnit.SECONDS))
            assertFalse("Audio->core lock inversion at the playback gate",heldMonitor)
            assertTrue("Freshness must be checked after waiting for the audio monitor",boundaryMonitor)
        }finally {release.countDown();audio.close()}
    }
}
