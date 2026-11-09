package ai.learndrive.mobile.navigation.google

import android.content.Context
import android.content.pm.PackageManager
import android.os.SystemClock
import android.util.Log
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest
import java.util.concurrent.Executors
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min

/**
 * Hills from the map: the elevation along Google's route, from the Google Maps Elevation API, so
 * the coach names a climb or descent and the gear before the car reaches it, whatever the phone
 * mount does.
 *
 * The route line is sampled every 20 m and sent a stretch at a time (up to 500 points, about
 * 10 km, per request; the next stretch once the car is within 4 km of the end of what is known;
 * at most 100 km per route). The heights stay in memory with the route and are dropped with it,
 * as Google's terms ask (no caching). The key goes only to Google, in the request; neither it
 * nor the request URL is logged or shown.
 */
class RouteElevation(
    private val context:Context,
    private val key:String,
    /** Held while a route line or its heights change (the route line's own lock). */
    private val lock:Any,
    /** All heights so far (lat, lon, metres), under [lock]: the climbs and descents found, or -1 if not taken. */
    private val apply:(DoubleArray)->Int,
    private val status:(State,String)->Unit,
):AutoCloseable {
    enum class State {NO_ROUTE,FETCHING,READY,DENIED,UNAVAILABLE}
    private val executor=Executors.newSingleThreadExecutor {Thread(it,"route-elevation").apply {isDaemon=true}}
    // Guarded by [lock].
    private var generation=0L
    private var points=DoubleArray(0) // lat, lon every 20 m along the route
    private var fetched=0             // points asked for and answered
    private var heights=DoubleArray(0)
    private var inFlight=false
    private var refused=false         // the key may not use the Elevation API: not asked again for this route
    private var failures=0
    private var retryAt=0L
    // The route these heights belong to: Google clears and republishes the same line when
    // navigation briefly loses the route, and its heights are used again, not asked for again.
    private var lineHash=0;private var lineSize=-1;private var lineAt=0L
    @Volatile private var closed=false

    /** Google's route line (lat, lon pairs) as it is loaded natively, or null when there is none. */
    fun route(line:DoubleArray?) {
        var found=-2;val count:Int;val wasRefused:Boolean
        synchronized(lock) {
            ++generation;inFlight=false
            val now=SystemClock.elapsedRealtime()
            if(line!=null&&line.size==lineSize&&line.contentHashCode()==lineHash&&now-lineAt<30*60_000L) {
                // The same route again: its heights so far go back to the reloaded line.
                if(heights.isNotEmpty())found=apply(heights)
            } else if(line!=null) {
                points=if(key.isNotBlank())resample(line) else DoubleArray(0)
                fetched=0;heights=DoubleArray(0);refused=false;failures=0;retryAt=0L
                lineHash=line.contentHashCode();lineSize=line.size;lineAt=now
            }
            count=points.size/2;wasRefused=refused
        }
        when {
            line==null->status(State.NO_ROUTE,"Choose a destination: hills ahead come from Google's elevation")
            key.isBlank()->status(State.UNAVAILABLE,"No Google key · the phone's tilt is used")
            count<2->status(State.NO_ROUTE,"Route too short to read its elevation")
            found>=0->status(State.READY,summary(found,count))
            wasRefused->status(State.DENIED,DENIED_TEXT)
            else->fetchNext()
        }
    }
    private fun summary(found:Int,count:Int):String {
        val known=Math.round(fetched*STEP/100)/10.0
        return "Google elevation · ${if(found==0)"no steep climbs or descents" else "$found ${if(found==1)"climb or descent" else "climbs and descents"}"}"+
            if(fetched<count)" in the first $known km" else " on this route"
    }

    /** Native coverage ([start m, end m, car m or -1, climbs and descents]): asks for the next stretch in time. */
    fun progress(cover:DoubleArray) {
        if(cover.size<3||cover[2]<0)return
        val due=synchronized(lock) {
            fetched<points.size/2&&!inFlight&&!refused&&SystemClock.elapsedRealtime()>=retryAt&&fetched*STEP-cover[2]<AHEAD
        }
        if(due)fetchNext()
    }

    private fun fetchNext() {
        val gen:Long;val upTo:Int;val chunk:DoubleArray
        synchronized(lock) {
            if(closed||inFlight||refused||fetched>=points.size/2)return
            inFlight=true;gen=generation;upTo=min(points.size/2,fetched+PER_REQUEST);chunk=points.copyOfRange(2*fetched,2*upTo)
        }
        if(gen>0&&chunk.isNotEmpty()&&upTo<=PER_REQUEST)status(State.FETCHING,"Reading the route's elevation…")
        executor.execute {
            val answer=try {Result.success(request(chunk))}catch(e:Exception){Result.failure(e)}
            var report:Pair<State,String>?=null
            synchronized(lock) {
                if(closed||gen!=generation)return@execute // The route changed meanwhile.
                inFlight=false
                answer.onSuccess {got->
                    heights+=got;fetched=upTo;failures=0
                    val found=apply(heights)
                    report=if(found<0)State.UNAVAILABLE to "Elevation did not match the route · the phone's tilt is used"
                    else State.READY to summary(found,points.size/2)
                }.onFailure {e->
                    val denied=e is Refused&&e.denied
                    if(denied)refused=true else {failures++;retryAt=SystemClock.elapsedRealtime()+min(120_000L,10_000L shl min(failures,4))}
                    // Only the kind of failure: an exception's text could carry the request.
                    Log.w("LearnDriveElevation","Elevation unavailable: ${if(e is Refused)e.message else e.javaClass.simpleName}")
                    val known=heights.isNotEmpty()
                    report=when {
                        denied->State.DENIED to DENIED_TEXT
                        e is Refused&&e.message=="quota"->State.UNAVAILABLE to "Elevation quota reached · the phone's tilt is used"
                        known->State.READY to "Google elevation · next stretch unavailable, retrying"
                        else->State.UNAVAILABLE to "Elevation unavailable (no connection) · retrying; the phone's tilt meanwhile"
                    }
                }
            }
            report?.let {(s,text)->status(s,text)}
        }
    }

    private class Refused(val denied:Boolean,message:String):Exception(message)

    /** One Elevation API request for [chunk] (lat, lon pairs); the answer as lat, lon, metres. */
    private fun request(chunk:DoubleArray):DoubleArray {
        val query="locations=enc:"+URLEncoder.encode(encode(chunk),"UTF-8")+"&key="+URLEncoder.encode(key,"UTF-8")
        val connection=(URL("https://maps.googleapis.com/maps/api/elevation/json?$query").openConnection() as HttpURLConnection).apply {
            connectTimeout=10_000;readTimeout=15_000;useCaches=false
            // For a key restricted to this Android app.
            setRequestProperty("X-Android-Package",context.packageName)
            certificate?.let {setRequestProperty("X-Android-Cert",it)}
        }
        try {
            val code=connection.responseCode
            val body=(if(code in 200..299)connection.inputStream else connection.errorStream)?.bufferedReader()?.use {it.readText()}.orEmpty()
            val json=try {JSONObject(body)}catch(_:Exception){throw Refused(false,"HTTP $code")}
            when(val state=json.optString("status")) {
                "OK"->Unit
                "REQUEST_DENIED"->throw Refused(true,"denied")
                "OVER_DAILY_LIMIT","OVER_QUERY_LIMIT"->throw Refused(false,"quota")
                else->throw Refused(false,state.ifBlank {"HTTP $code"})
            }
            val results=json.getJSONArray("results")
            val out=DoubleArray(results.length()*3);var n=0
            for(i in 0 until results.length()) {
                val r=results.getJSONObject(i);val at=r.optJSONObject("location")?:continue
                val metres=r.optDouble("elevation",Double.NaN);val resolution=r.optDouble("resolution",0.0)
                // Heights interpolated from data points over 200 m apart cannot show a 60 m slope.
                if(!metres.isFinite()||resolution>200)continue
                out[3*n]=at.getDouble("lat");out[3*n+1]=at.getDouble("lng");out[3*n+2]=metres;n++
            }
            return out.copyOf(3*n)
        } finally {connection.disconnect()}
    }

    /** SHA-1 of this app's signing certificate, as Google's Android key restriction expects it. */
    private val certificate:String? by lazy {
        try {
            @Suppress("DEPRECATION")
            val signing=context.packageManager.getPackageInfo(context.packageName,PackageManager.GET_SIGNING_CERTIFICATES).signingInfo
                ?:return@lazy null
            val cert=(if(signing.hasMultipleSigners())signing.apkContentsSigners.firstOrNull() else signing.signingCertificateHistory.lastOrNull())
                ?:return@lazy null
            MessageDigest.getInstance("SHA-1").digest(cert.toByteArray()).joinToString("") {"%02X".format(it)}
        } catch(_:Exception) {null}
    }

    override fun close() {
        closed=true;executor.shutdownNow()
        synchronized(lock) {++generation;points=DoubleArray(0);heights=DoubleArray(0);lineSize=-1}
    }

    companion object {
        private const val STEP=20.0
        private const val PER_REQUEST=500
        private const val AHEAD=4000.0
        private const val MAX_ROUTE=100_000.0
        private const val DENIED_TEXT="Elevation API not enabled for this key · the phone's tilt is used"

        /** Points every [STEP] m along [line] (lat, lon pairs), up to [MAX_ROUTE]. */
        internal fun resample(line:DoubleArray):DoubleArray {
            if(line.size<4)return DoubleArray(0)
            val out=ArrayList<Double>(min(line.size,2*(MAX_ROUTE/STEP).toInt()+4))
            out.add(line[0]);out.add(line[1])
            var carry=0.0;var total=0.0
            for(i in 1 until line.size/2) {
                val la0=line[2*i-2];val lo0=line[2*i-1];val la1=line[2*i];val lo1=line[2*i+1]
                val d=hypot((lo1-lo0)*111_320*cos(Math.toRadians((la0+la1)/2)),(la1-la0)*110_540)
                if(!(d>0))continue
                var at=STEP-carry
                while(at<=d) {
                    val u=at/d;out.add(la0+u*(la1-la0));out.add(lo0+u*(lo1-lo0))
                    total+=STEP;if(total>=MAX_ROUTE)return out.toDoubleArray()
                    at+=STEP
                }
                carry=d-(at-STEP)
            }
            if(carry>5){out.add(line[line.size-2]);out.add(line[line.size-1])}
            return out.toDoubleArray()
        }

        /** Google's encoded polyline of lat, lon pairs. */
        internal fun encode(points:DoubleArray):String {
            val text=StringBuilder(points.size*3);var lastLat=0L;var lastLon=0L
            fun put(value:Long) {
                var v=if(value<0)(value shl 1).inv() else value shl 1
                while(v>=0x20){text.append(((0x20L or (v and 0x1f))+63).toInt().toChar());v=v shr 5}
                text.append((v+63).toInt().toChar())
            }
            for(i in 0 until points.size/2) {
                val lat=Math.round(points[2*i]*1e5);val lon=Math.round(points[2*i+1]*1e5)
                put(lat-lastLat);put(lon-lastLon);lastLat=lat;lastLon=lon
            }
            return text.toString()
        }
    }
}
