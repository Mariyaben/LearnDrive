package ai.learndrive.mobile.navigation

import com.valhalla.valhalla.Valhalla
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** Own this on one dedicated worker. No HTTP tile URL, remote routing or geocoder. */
class OfflineRouter(private val pack:OfflinePack,private val template:String):AutoCloseable {
    private var actor:Valhalla?=null
    var initializationMs=0.0;private set
    var routeMs=0.0;private set
    fun route(startLat:Double,startLon:Double,destLat:Double,destLon:Double):OfflineRoute {
        require(pack.contains(startLat,startLon)&&pack.contains(destLat,destLon)){"Start or destination is outside installed coverage"}
        if(actor==null) {
            val start=System.nanoTime()
            // App-owned template; never execute a configuration supplied by a pack.
            val config=JSONObject(template).apply {
                getJSONObject("mjolnir").put("tile_extract",pack.tiles.absolutePath)
                    .put("tile_dir",File(pack.folder,"unused-loose-tiles").absolutePath)
                    .put("max_cache_size",64*1024*1024).remove("tile_url")
            }
            val file=File(pack.folder,"runtime-config.json");file.writeText(config.toString())
            actor=Valhalla(file.absolutePath);initializationMs=(System.nanoTime()-start)/1e6
        }
        val request=JSONObject().put("locations",JSONArray(listOf(JSONObject().put("lat",startLat).put("lon",startLon),
            JSONObject().put("lat",destLat).put("lon",destLon))))
            .put("costing","auto").put("units","kilometers").put("shape_format","polyline6")
            .put("directions_options",JSONObject().put("units","kilometers").put("language","en-US"))
        val start=System.nanoTime();val raw=actor!!.routeRaw(request.toString());routeMs=(System.nanoTime()-start)/1e6
        return OfflineRoute.fromValhalla(raw,pack)
    }
    override fun close(){actor?.close();actor=null}
}
