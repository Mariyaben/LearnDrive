package ai.learndrive.mobile

data class DeskRow(val time:Double,val speed:Double,val pitch:Double,val braking:Boolean,val tti:Double,val confidence:Double,val path:Boolean,val gear:Boolean)
class DeskScript(text:String) {
    private val rows:List<DeskRow>
    init {
        val lines=text.lineSequence().filter {it.isNotBlank()}.toList()
        require(lines.firstOrNull()?.trim()=="time,speed_kmh,pitch_deg,braking,tti_s,confidence,in_path,allow_gear") {"Unexpected CSV header"}
        rows=lines.drop(1).map { line ->
            val v=line.trim().split(',').map {it.toDouble()};require(v.size==8&&v.all {it.isFinite()}) {"Eight finite CSV values required"}
            require(v[0]>=0&&v[1] in 0.0..160.0&&v[2] in -30.0..30.0&&v[4]>0&&v[5] in 0.0..1.0)
            DeskRow(v[0],v[1],v[2],v[3]!=0.0,v[4],v[5],v[6]!=0.0,v[7]!=0.0)
        }
        require(rows.isNotEmpty()&&rows.zipWithNext().all {it.first.time<it.second.time}) {"CSV timestamps must increase"}
    }
    fun at(time:Double):DeskRow = rows.lastOrNull {it.time<=time}?:rows.first()
}
