package ai.learndrive.mobile

import android.os.SystemClock
import android.util.Log
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.util.concurrent.Executors
import kotlin.math.cos
import kotlin.math.min

/**
 * Road geometry around the car for junction-ahead coaching WITHOUT a destination.
 *
 * Fetches OpenStreetMap roads (© OpenStreetMap contributors, ODbL) for a ~1.8 km
 * square around the car from the public Overpass API, on a background thread,
 * and hands them to the native junction scout. Only the square's bounds are sent
 * (never the exact position); a generic client name is used. Refetches only when
 * the car nears the edge of covered roads; backs off after failures.
 */
class RoadNetwork(private val core:NativeCore,private val status:(String)->Unit={}):AutoCloseable {
    private val executor=Executors.newSingleThreadExecutor {Thread(it,"osm-roads")}
    private val covered=java.util.concurrent.CopyOnWriteArrayList<DoubleArray>() // south, west, north, east
    @Volatile private var inFlight=false
    @Volatile private var closed=false
    private var failures=0
    @Volatile private var retryAt=0L
    @Volatile var roads=0;private set
    @Volatile var bumps=0;private set

    fun onLocation(lat:Double,lon:Double) {
        if(closed||inFlight||!lat.isFinite()||!lon.isFinite()||SystemClock.elapsedRealtime()<retryAt)return
        // Keep ~300 m of known road around the car (enough for the 400 m look-ahead to be useful).
        val marginLat=300/110540.0;val marginLon=300/(111320*cos(Math.toRadians(lat)))
        if(covered.any {lat-marginLat>=it[0]&&lon-marginLon>=it[1]&&lat+marginLat<=it[2]&&lon+marginLon<=it[3]})return
        inFlight=true
        executor.execute {
            try {fetch(lat,lon);failures=0}
            catch(e:Exception) {
                failures++;retryAt=SystemClock.elapsedRealtime()+min(120_000L,5_000L shl min(failures,5))
                Log.w("LearnDriveRoads","Road map unavailable: ${e.message}");status(if(roads>0)"Road map: $roads roads · update failed, retrying" else "Road map unavailable (${e.message}); retrying")
            }finally {inFlight=false}
        }
    }

    private fun fetch(lat:Double,lon:Double) {
        // Snap to a ~0.9 km grid so requests reveal only a coarse area.
        val step=.008;val cLat=Math.round(lat/step)*step;val cLon=Math.round(lon/step)*step
        val box=doubleArrayOf(cLat-step,cLon-step,cLat+step,cLon+step)
        // Roads, plus mapped speed bumps (traffic calming) for "Speed bump ahead".
        val bbox="(${box[0]},${box[1]},${box[2]},${box[3]})"
        val query="[out:json][timeout:20];(way[\"highway\"~\"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street|service|track)(_link)?$\"]"+
            "$bbox;node[\"traffic_calming\"]$bbox;);out body geom;"
        status("Loading road map")
        val connection=(URL("https://overpass-api.de/api/interpreter").openConnection() as HttpURLConnection).apply {
            requestMethod="POST";doOutput=true;connectTimeout=10_000;readTimeout=25_000
            setRequestProperty("User-Agent","LearnDrive driving coach")
            setRequestProperty("Content-Type","application/x-www-form-urlencoded")
        }
        try {
            connection.outputStream.use {it.write(("data="+URLEncoder.encode(query,"UTF-8")).toByteArray())}
            check(connection.responseCode==200) {"HTTP ${connection.responseCode}"}
            val elements=JSONObject(connection.inputStream.bufferedReader().use {it.readText()}).getJSONArray("elements")
            var added=0;val bumpNodes=ArrayList<Long>()
            for(i in 0 until elements.length()) {
                val way=elements.getJSONObject(i)
                if(way.optString("type")=="node"){if(way.optJSONObject("tags")?.optString("traffic_calming").let {it!=null&&it!="no"&&it.isNotEmpty()})bumpNodes.add(way.getLong("id"));continue}
                if(way.optString("type")!="way")continue
                val cls=roadClass(way.optJSONObject("tags")?.optString("highway")?:continue)?:continue
                val nodes=way.optJSONArray("nodes")?:continue;val geometry=way.optJSONArray("geometry")?:continue
                if(nodes.length()<2||nodes.length()!=geometry.length())continue
                val ids=LongArray(nodes.length()) {nodes.getLong(it)}
                val latLon=DoubleArray(ids.size*2)
                for(k in ids.indices){val p=geometry.getJSONObject(k);latLon[2*k]=p.getDouble("lat");latLon[2*k+1]=p.getDouble("lon")}
                if(!closed&&core.roadWay(way.getLong("id"),cls,ids,latLon))added++
            }
            if(!closed)bumpNodes.forEach {core.roadBump(it)}
            covered.add(box);roads+=added;bumps+=bumpNodes.size
            status("Road map: $roads roads, $bumps bumps (© OpenStreetMap contributors)")
        }finally {connection.disconnect()}
    }

    override fun close() {closed=true;executor.shutdownNow()}

    companion object {
        /** OSM highway value to the native road class (0 motorway .. 9 track); null = not a driving road. */
        fun roadClass(highway:String):Int? = when(highway.removeSuffix("_link")) {
            "motorway"->0;"trunk"->1;"primary"->2;"secondary"->3;"tertiary"->4;"unclassified"->5
            "residential"->6;"living_street"->7;"service"->8;"track"->9;else->null
        }
    }
}
