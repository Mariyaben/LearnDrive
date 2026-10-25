package ai.learndrive.mobile.navigation.google

import android.app.Service
import android.content.Intent
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.os.SystemClock
import com.google.android.libraries.mapsplatform.turnbyturn.TurnByTurnManager
import com.google.android.libraries.mapsplatform.turnbyturn.model.NavInfo

/** Default-process, non-exported Messenger service registered only while supervised navigation runs. */
class GoogleNavigationUpdatesService:Service() {
    private lateinit var decoder:TurnByTurnManager
    private val handlers=ArrayList<Handler>()
    override fun onCreate() {
        super.onCreate()
        decoder=TurnByTurnManager.createInstance()
    }
    override fun onBind(intent:Intent?):IBinder {
        // A previous SDK binding can deliver queued messages after unregistering.
        // Capture ownership in the binder's Handler, not when each message arrives.
        val bindingOwner=GoogleNavigationFeed.ownerToken()
        val handler=object:Handler(Looper.getMainLooper()) {
            override fun handleMessage(message:Message) {
                if(message.what!=TurnByTurnManager.MSG_NAV_INFO){super.handleMessage(message);return}
                if(!GoogleNavigationFeed.isOwner(bindingOwner))return
                val info=runCatching {decoder.readNavInfoFromBundle(message.data)}.getOrNull()
                GoogleNavigationFeed.publish(bindingOwner,info,SystemClock.elapsedRealtimeNanos()/1e9)
            }
        }
        handlers.add(handler)
        return Messenger(handler).binder
    }
    override fun onUnbind(intent:Intent?):Boolean {clearHandlers();return false}
    override fun onDestroy() {clearHandlers();super.onDestroy()}
    private fun clearHandlers() {handlers.forEach {it.removeCallbacksAndMessages(null)};handlers.clear()}
}

/** In-process delivery only; never persists or broadcasts navigation content. */
internal object GoogleNavigationFeed {
    private var owner=0L
    private var listener:((NavInfo?,Double)->Unit)?=null
    fun attach(id:Long,receive:(NavInfo?,Double)->Unit) {owner=id;listener=receive}
    fun detach(id:Long) {if(owner==id){owner=0;listener=null}}
    fun ownerToken()=owner
    fun isOwner(id:Long)=id>0&&id==owner
    fun publish(id:Long,info:NavInfo?,receivedAt:Double) {if(isOwner(id))listener?.invoke(info,receivedAt)}
}
