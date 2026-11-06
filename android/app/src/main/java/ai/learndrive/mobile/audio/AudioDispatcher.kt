package ai.learndrive.mobile.audio

import ai.learndrive.mobile.AudioWorker

/** Native Action/Prompt IDs select cached clips; no synthesis occurs here. */
class AudioDispatcher(private val worker:AudioWorker) {
    // Called synchronously by the native reflex on the attached 100 Hz JNI
    // rules thread. Stop queued/current PCM before the next sentence is chosen.
    @Suppress("unused")
    fun onReflex(actionId:Int) {if(actionId==4||actionId==42||actionId==43)worker.preempt()}
    fun dispatch(actionId:Int)=worker.submit(actionId)
}
