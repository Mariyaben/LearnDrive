package ai.learndrive.mobile.navigation

import org.json.JSONObject

data class OfflineRoute(val id:Long,val packId:String,val shape:DoubleArray,val maneuvers:IntArray,val lengthKm:Double,val instructions:Map<Int,String> = emptyMap(),val destinationLabel:String="") {
    fun json()=JSONObject().put("id",id).put("pack_id",packId).put("shape",org.json.JSONArray(shape.toList()))
        .put("maneuvers",org.json.JSONArray(maneuvers.toList())).put("length_km",lengthKm)
        .put("destination_label",destinationLabel.take(160))
        .put("instructions",JSONObject().apply {instructions.forEach {(key,value)->put(key.toString(),value)}}).toString()
    companion object {
        fun saved(text:String):OfflineRoute {
            val j=JSONObject(text);val a=j.getJSONArray("shape");val m=j.getJSONArray("maneuvers")
            require(a.length() in 4..200000&&a.length()%2==0&&m.length()<=20000&&m.length()%2==0)
            val id=j.getLong("id");require(id>0)
            val steps=j.optJSONObject("instructions")?:JSONObject()
            require(steps.length()<=10000)
            return OfflineRoute(id,j.getString("pack_id"),DoubleArray(a.length()){a.getDouble(it)},IntArray(m.length()){m.getInt(it)},j.getDouble("length_km"),
                steps.keys().asSequence().associate {it.toInt() to steps.getString(it).take(250)},j.optString("destination_label","").take(160))
        }
        fun fromValhalla(text:String,pack:OfflinePack):OfflineRoute {
            require(text.length<=8*1024*1024){"Route response too large"}
            val root=JSONObject(text)
            require(!root.has("error")&&!root.has("error_code")&&!root.has("code")){root.optString("message",root.optString("error","Offline route unavailable"))}
            val trip=root.getJSONObject("trip");require(trip.getInt("status")==0)
            require(trip.getString("units")=="kilometers"){"Unexpected route distance units"}
            val legs=trip.getJSONArray("legs");require(legs.length()==1){"Only direct, single-leg routes are currently supported"}
            val leg=legs.getJSONObject(0);val shape=decodePolyline6(leg.getString("shape"))
            require(shape.size in 4..200000)
            for(i in shape.indices step 2)require(pack.contains(shape[i],shape[i+1])){"Route leaves installed coverage; install a larger pack"}
            val turns=leg.getJSONArray("maneuvers");require(turns.length()<=10000)
            val maneuvers=IntArray(turns.length()*2)
            val instructions=HashMap<Int,String>()
            for(i in 0 until turns.length()) {
                val m=turns.getJSONObject(i);val index=m.getInt("begin_shape_index")
                require(index in 0 until shape.size/2)
                maneuvers[i*2]=index;maneuvers[i*2+1]=m.getInt("type")
                instructions[index]=m.optString("instruction","").take(250)
            }
            val length=trip.getJSONObject("summary").getDouble("length");require(length.isFinite()&&length>0&&length<=1000)
            return OfflineRoute(System.currentTimeMillis(),pack.id,shape,maneuvers,length,instructions)
        }
        fun decodePolyline6(encoded:String):DoubleArray {
            require(encoded.length<=1500000)
            val result=ArrayList<Double>();var cursor=0;var lat=0L;var lon=0L
            fun delta():Long {
                var value=0L;var shift=0
                while(true) {
                    require(cursor<encoded.length&&shift<=30){"Malformed route shape"}
                    val b=encoded[cursor++].code-63;require(b in 0..63)
                    value=value or ((b and 31).toLong() shl shift);shift+=5
                    if(b<32)break
                }
                return if(value and 1L!=0L)(value shr 1).inv() else value shr 1
            }
            while(cursor<encoded.length) {
                lat+=delta();lon+=delta();require(lat in -85000000..85000000&&lon in -180000000..180000000)
                result.add(lat/1e6);result.add(lon/1e6);require(result.size<=200000)
            }
            return result.toDoubleArray()
        }
    }
}
