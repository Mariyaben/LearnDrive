package ai.learndrive.mobile

/** Short-lived caption of accepted speech, never a persistent driving command. */
class CoachCaption {
    private data class Entry(val text:String,val source:Long,val acceptedAt:Double,val expiresAt:Double,
        val route:Long?,val eventProgress:Double?,val origin:Int)
    data class Presentation(val text:String,val context:String)
    private var entry:Entry?=null

    fun accept(text:String,source:Long,time:Double,duration:Double,navigation:DoubleArray?=null,origin:Int=0) {
        if(text.isBlank()||!time.isFinite()||!duration.isFinite()){clear();return}
        val route=navigation?.takeIf {it.size>=17&&it[10]!=0.0}?.get(0)?.toLong()
        val progress=if(route!=null&&navigation!![5].isFinite()&&navigation[6].isFinite())navigation[5]+navigation[6] else null
        entry=Entry(text,source,time,time+(duration+2).coerceIn(5.0,12.0),route,progress,origin)
    }
    fun visible(time:Double,source:Long,active:Boolean,navigation:DoubleArray?=null):String? =
        presentation(time,source,active,navigation)?.text

    fun presentation(time:Double,source:Long,active:Boolean,navigation:DoubleArray?=null):Presentation? {
        val value=entry?:return null
        val navigationExpired=value.route!=null&&(navigation==null||navigation.size<17||navigation[10]==0.0||
            navigation[0].toLong()!=value.route||(value.eventProgress!=null&&navigation[5]>=value.eventProgress-5))
        if(!active||source!=value.source||!time.isFinite()||time<value.acceptedAt||time>=value.expiresAt||navigationExpired) {
            clear();return null
        }
        // This is the recently accepted cue, not a promise that its evidence is
        // still current or that the driver/phone speaker carried out the cue.
        val evidence=when(value.origin) {
            // A coordinator stage is not a sensor: road preparation includes
            // both camera bends and phone inclination. Do not invent origin.
            1->"road scene";2->"driving";3->"road preparation";4->"camera";5->"navigation";else->"coach"
        }
        val seconds=(time-value.acceptedAt).toInt()
        return Presentation(value.text,"Recent $evidence cue · ${seconds}s ago")
    }
    fun clear(){entry=null}
    fun clearNavigation(){if(entry?.route!=null)clear()}
}
