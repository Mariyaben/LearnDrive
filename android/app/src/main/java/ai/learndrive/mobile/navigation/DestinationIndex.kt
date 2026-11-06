package ai.learndrive.mobile.navigation

import org.json.JSONArray
import java.io.File
import java.text.Normalizer
import java.util.Locale
import android.database.sqlite.SQLiteDatabase

data class DestinationPlace(val name:String,val latitude:Double,val longitude:Double,val kind:String,
    val locality:String,val pack:OfflinePack,val aliases:List<String> = emptyList()) {
    val searchable=normalize((listOf(name)+aliases+locality).joinToString(" "))
    val normalizedName=normalize(name)
    val detail=listOf(kind.replace('_',' '),locality).filter {it.isNotBlank()}.joinToString(" · ")
    override fun toString()=name
    companion object {
        private val marks=Regex("\\p{M}+")
        private val separators=Regex("[^\\p{L}\\p{N}]+")
        fun normalize(value:String)=Normalizer.normalize(value.lowercase(Locale.ROOT),Normalizer.Form.NFD)
            .replace(marks,"").replace(separators," ").trim()
    }
}

class DestinationIndex(val places:List<DestinationPlace>,private val database:SQLiteDatabase?=null,
    private val pack:OfflinePack?=null,private val children:List<DestinationIndex> = emptyList(),val size:Int=places.size):AutoCloseable {
    fun search(query:String):List<DestinationPlace> {
        val q=DestinationPlace.normalize(query.take(160));if(q.length<2)return emptyList()
        if(children.isNotEmpty())return children.flatMap {it.search(q)}.take(20)
        val tokens=q.split(' ').filter {it.isNotEmpty()}.take(8)
        if(database!=null) {
            val result=ArrayList<DestinationPlace>()
            database.rawQuery("SELECT d.name,d.lat,d.lon,d.kind,d.locality FROM lookup JOIN destinations d ON d.rowid=lookup.docid WHERE lookup MATCH ? ORDER BY CASE WHEN d.name_key=? THEN 0 WHEN d.name_key LIKE ? THEN 1 ELSE 2 END,d.kind_rank,length(d.name),d.name LIMIT 20",
                arrayOf(tokens.joinToString(" "){"$it*"},q,"$q%")).use {cursor->
                while(cursor.moveToNext()) {
                    val lat=cursor.getDouble(1);val lon=cursor.getDouble(2)
                    if(pack!!.contains(lat,lon))result.add(DestinationPlace(cursor.getString(0).take(160),lat,lon,cursor.getString(3).take(60),cursor.getString(4).take(120),pack))
                }
            }
            return result
        }
        return places.asSequence().filter {p->tokens.all {it in p.searchable}}
            .sortedWith(compareBy<DestinationPlace> {
                when {it.normalizedName==q->0;it.normalizedName.startsWith(q)->1;it.searchable.startsWith(q)->2;else->3}
            }.thenBy {if(it.kind in listOf("city","town","village","suburb"))0 else 1}.thenBy {it.name.length}.thenBy {it.name})
            .take(20).toList()
    }
    override fun close(){children.forEach {it.close()};database?.close()}
    companion object {
        fun combine(parts:List<DestinationIndex>)=DestinationIndex(emptyList(),children=parts,size=parts.sumOf {it.size})
        fun load(pack:OfflinePack):DestinationIndex {
            val sqlite=File(pack.folder,"places.sqlite")
            if(sqlite.isFile) {
                require(sqlite.length() in 1..96L*1024*1024)
                val db=SQLiteDatabase.openDatabase(sqlite.absolutePath,null,SQLiteDatabase.OPEN_READONLY or SQLiteDatabase.NO_LOCALIZED_COLLATORS)
                try {
                    db.rawQuery("PRAGMA application_id",null).use {require(it.moveToFirst()&&it.getInt(0)==0x4C444E56)}
                    db.rawQuery("PRAGMA user_version",null).use {require(it.moveToFirst()&&it.getInt(0)==1)}
                    db.rawQuery("SELECT count(*) FROM sqlite_master WHERE type IN ('trigger','view')",null).use {require(it.moveToFirst()&&it.getInt(0)==0)}
                    val count=db.rawQuery("SELECT count(*) FROM destinations",null).use {require(it.moveToFirst());it.getInt(0)}
                    require(count in 1..500000)
                    return DestinationIndex(emptyList(),db,pack,size=count)
                }catch(e:Exception){db.close();throw e}
            }
            val file=File(pack.folder,"places.json");if(!file.isFile)return DestinationIndex(emptyList())
            require(file.length()<=4L*1024*1024){"This region needs its updated search database"}
            val rows=JSONArray(file.readText());require(rows.length()<=20000)
            return DestinationIndex((0 until rows.length()).mapNotNull {i->
                val j=rows.getJSONObject(i);val lat=j.getDouble("lat");val lon=j.getDouble("lon")
                if(!pack.contains(lat,lon))null else {
                    val names=j.optJSONArray("aliases")
                    DestinationPlace(j.getString("name").take(160),lat,lon,j.optString("kind","place").take(60),
                        j.optString("locality","").take(120),pack,
                        (0 until minOf(names?.length()?:0,8)).map {names!!.getString(it).take(160)})
                }
            })
        }
    }
}
