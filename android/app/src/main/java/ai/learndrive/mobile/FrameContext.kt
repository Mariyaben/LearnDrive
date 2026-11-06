package ai.learndrive.mobile

/** A source generation identifies one continuous capture session. */
enum class CaptureSource(val nativeId:Int) { CAMERA(0), RECORDING(1), SIMULATION(2), SCREEN(3) }
data class FrameContext(val sourceId:Long,val source:CaptureSource,val captureTime:Double,
    val recordedTime:Double?=null,val intrinsics:DoubleArray?=null,
    val calibrationOrigin:String="unknown",val imageWidth:Int=0,val imageHeight:Int=0,
    val rotationDegrees:Int=0,val cropLeft:Int=0,val cropTop:Int=0,val cropWidth:Int=0,val cropHeight:Int=0,
    val pixelRange:String="unknown",val pixelRangeOrigin:String="assumed video range",val pixelFormat:Int=0,
    val frameSequence:Long=0)
class StaleSourceException:IllegalStateException("Frame belongs to an earlier source")

data class GuidanceDecision(val sequence:Long,val sourceId:Long,val prompt:Int,val origin:Int,
    val tactical:Int,val reflex:Int,val terrain:Int,val camera:Int,val suppress:Boolean,
    val eligible:Boolean,val blocked:Int,val captureTime:Double,val sceneAge:Double,
    val semanticAvailable:Boolean,val spatialAvailable:Boolean,val decidedAt:Double,
    val actorId:Long=0,val actorCategory:Int=-1,val evidenceKind:Int=0,val actorBox:List<Double> = emptyList(),
    val settingsRevision:Long=0) {
    companion object {
        fun from(v:DoubleArray,time:Double)=GuidanceDecision(v[0].toLong(),v[1].toLong(),v[2].toInt(),v[3].toInt(),
            v[4].toInt(),v[5].toInt(),v[6].toInt(),v[7].toInt(),v[8]!=0.0,v[9]!=0.0,v[10].toInt(),
            v[11],v[12],v[13]!=0.0,v[14]!=0.0,time,v[15].toLong(),v[16].toInt(),v[17].toInt(),v.slice(18..21))
    }
}
