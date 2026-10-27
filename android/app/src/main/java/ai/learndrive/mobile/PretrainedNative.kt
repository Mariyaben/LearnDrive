package ai.learndrive.mobile

/** A separate local runtime. No NativeCore monitor or speech authority is used. */
object PretrainedNative {
    val linked:Boolean=runCatching {System.loadLibrary("learndrive_reasoning");available()}.getOrDefault(false)
    private external fun available():Boolean
    external fun create():Long
    external fun load(handle:Long,path:String,kind:Int):Boolean
    external fun choose(handle:Long,context:String,choices:String,timeoutMs:Int,system:String):String
    external fun cancel(handle:Long)
    external fun error(handle:Long):String
    external fun close(handle:Long)
}
