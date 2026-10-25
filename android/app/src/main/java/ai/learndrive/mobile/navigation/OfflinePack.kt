package ai.learndrive.mobile.navigation

import android.content.Context
import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipInputStream

data class OfflinePack(val folder:File,val metadata:JSONObject) {
    val id=metadata.getString("id")
    val name=metadata.getString("name")
    val bounds=metadata.getJSONArray("bounds").let {DoubleArray(4){i->it.getDouble(i)}}
    val tiles get()=File(folder,"routing/tiles.tar")
    val map get()=File(folder,"map.pmtiles")
    fun contains(lat:Double,lon:Double)=lat.isFinite()&&lon.isFinite()&&lon in bounds[0]..bounds[2]&&lat in bounds[1]..bounds[3]
    companion object {
        const val MAX_BYTES=2L*1024*1024*1024
        private val names=setOf("routing/tiles.tar","map.pmtiles","places.json","places.sqlite")
        fun installed(context:Context):List<OfflinePack> = File(context.filesDir,"navigation/packs").listFiles()?.mapNotNull {
            runCatching {val f=File(it,"manifest.json");require(f.length() in 1..65536);validate(it,JSONObject(f.readText()))}.getOrNull()
        }?.sortedBy {it.name}?:emptyList()
        fun validate(folder:File,j:JSONObject):OfflinePack {
            require(j.getInt("schema_version")==1&&j.getString("routing_engine")=="valhalla-3.9.1")
            require(j.getString("id").matches(Regex("[a-z0-9][a-z0-9_-]{0,63}")))
            require(j.getString("name").length in 1..120)
            require(j.getString("map_schema")=="protomaps"||j.getString("map_schema")=="openmaptiles")
            require(j.getString("attribution").contains("OpenStreetMap"))
            val b=j.getJSONArray("bounds");require(b.length()==4)
            val v=DoubleArray(4){b.getDouble(it)};require(v.all {it.isFinite()}&&v[0]>=-180&&v[2]<=180&&v[1]>=-85&&v[3]<=85&&v[0]<v[2]&&v[1]<v[3])
            val files=j.getJSONObject("files");require(files.has("routing/tiles.tar")&&files.has("map.pmtiles")&&files.keys().asSequence().all {it in names})
            files.keys().forEach {name->val m=files.getJSONObject(name);val f=File(folder,name)
                require(m.getLong("bytes") in 1..MAX_BYTES&&m.getString("sha256").matches(Regex("[a-f0-9]{64}")))
                require(f.isFile&&f.length()==m.getLong("bytes")){"Incomplete map pack: $name"}
            }
            val map=File(folder,"map.pmtiles");val header=ByteArray(127)
            val search=File(folder,"places.sqlite")
            if(files.has("places.sqlite")) {
                require(search.length()<=96L*1024*1024){"Destination index exceeds its limit"}
                val signature=ByteArray(16);search.inputStream().use {require(it.read(signature)==16)}
                require(String(signature,Charsets.US_ASCII)=="SQLite format 3\u0000"){"Invalid destination database"}
            }
            map.inputStream().use {require(it.read(header)==127){"Incomplete PMTiles header"}}
            require(String(header,0,7,Charsets.US_ASCII)=="PMTiles"&&header[7].toInt()==3&&header[99].toInt()==1){"Expected PMTiles v3 vector data"}
            val fields=java.nio.ByteBuffer.wrap(header).order(java.nio.ByteOrder.LITTLE_ENDIAN)
            for(offset in listOf(8,24,40,56)) {
                val start=fields.getLong(offset);val length=fields.getLong(offset+8)
                require(start>=127&&length>=0&&start<=map.length()&&length<=map.length()-start){"Invalid PMTiles section bounds"}
            }
            return OfflinePack(folder,j)
        }
        fun import(context:Context,input:InputStream):OfflinePack {
            val root=File(context.filesDir,"navigation/packs").apply {mkdirs()}
            val stage=File(root,"import-${UUID.randomUUID()}").apply {mkdirs()}
            try {
                var metadata:JSONObject?=null;var total=0L;val seen=HashSet<String>();val buffer=ByteArray(65536)
                ZipInputStream(input.buffered()).use {zip->
                    while(true) {
                        val entry=zip.nextEntry?:break;val name=entry.name
                        require(!entry.isDirectory&&seen.add(name)){"Duplicate or directory entry"}
                        if(name=="manifest.json") {
                            require(seen.size==1){"Manifest must be first"}
                            val bytes=zip.readBytesBounded(65536);metadata=JSONObject(String(bytes,Charsets.UTF_8))
                            File(stage,name).writeBytes(bytes)
                        } else {
                            require(name in names&&metadata!=null){"Unsupported pack entry"}
                            val expected=metadata!!.getJSONObject("files").getJSONObject(name)
                            val maximum=expected.getLong("bytes");require(maximum in 1..MAX_BYTES)
                            val dest=File(stage,name);dest.parentFile!!.mkdirs();val digest=MessageDigest.getInstance("SHA-256");var size=0L
                            dest.outputStream().use {out->while(true) {
                                val n=zip.read(buffer);if(n<0)break;size+=n;total+=n
                                require(size<=maximum&&total<=MAX_BYTES){"Map pack exceeds its size limit"}
                                require(root.usableSpace>16L*1024*1024){"Not enough storage for map pack"}
                                digest.update(buffer,0,n);out.write(buffer,0,n)
                            }}
                            require(size==maximum&&digest.digest().joinToString(""){"%02x".format(it)}==expected.getString("sha256")){"Map pack integrity check failed"}
                        }
                        zip.closeEntry()
                    }
                }
                val pack=validate(stage,requireNotNull(metadata));val dest=File(root,pack.id)
                require(!dest.exists()){"This pack is already installed"}
                // Only managed staging paths are recursively removed on failure.
                require(stage.renameTo(dest)){"Could not install map pack"}
                return OfflinePack(dest,pack.metadata)
            } finally {if(stage.exists())stage.deleteRecursively()}
        }
        private fun InputStream.readBytesBounded(maximum:Int):ByteArray {
            val out=java.io.ByteArrayOutputStream();val b=ByteArray(4096)
            while(true){val n=read(b);if(n<0)break;require(out.size()+n<=maximum);out.write(b,0,n)}
            return out.toByteArray()
        }
    }
}
