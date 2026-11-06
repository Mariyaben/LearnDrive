package ai.learndrive.mobile.navigation

import android.content.Context
import android.graphics.Color
import android.location.Location
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import android.widget.*
import androidx.activity.ComponentActivity
import androidx.core.widget.doAfterTextChanged
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger
import ai.learndrive.mobile.ui.CoachStyle

/** Destination setup inside the camera activity. Routing never owns capture or speech. */
class DestinationPanel(private val activity:ComponentActivity,private val position:()->Location?,
    private val requestLocation:()->Unit,private val routeSaved:()->Unit,private val routeCleared:()->Unit,
    private val packImported:(OfflinePack)->Unit={}):AutoCloseable {
    private val main=Handler(Looper.getMainLooper())
    private val worker=Executors.newSingleThreadExecutor {r->Thread(null,r,"destination-router",2L*1024*1024)}
    private val revision=AtomicInteger()
    private var closed=false
    private var editable=true
    private var running=false
    private var busy=false
    private var settingText=false
    private var waiting:DestinationPlace?=null
    private var router:OfflineRouter?=null // Worker only.
    private var routerPack=""
    @Volatile private var index=DestinationIndex(emptyList())
    val view=LinearLayout(activity).apply {orientation=LinearLayout.VERTICAL;setPadding(0,CoachStyle.dp(activity,12),0,CoachStyle.dp(activity,12))}
    val search=AutoCompleteTextView(activity).apply {
        tag="destination-search";hint="Where to? Town, landmark or business";contentDescription="Destination"
        setTextColor(CoachStyle.text);setHintTextColor(CoachStyle.muted);textSize=17f;setSingleLine();threshold=2
        background=CoachStyle.panel(activity,CoachStyle.ink,16);minimumHeight=CoachStyle.dp(activity,52)
        setPadding(CoachStyle.dp(activity,14),0,CoachStyle.dp(activity,14),0)
        dropDownHeight=(180*resources.displayMetrics.density).toInt()
        imeOptions=android.view.inputmethod.EditorInfo.IME_ACTION_SEARCH or android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI or android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN
        inputType=android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_CAP_WORDS
    }
    private val message=TextView(activity).apply {tag="destination-status";setTextColor(Color.rgb(173,205,219));textSize=14f;setPadding(0,4,0,4)}
    private val clear=CoachStyle.button(activity,"Clear").apply {setOnClickListener {clearDestination()}}
    private val locate=CoachStyle.button(activity,"Enable location").apply {setOnClickListener {if(editable)requestLocation()}}
    private val results=object:BaseAdapter(),Filterable {
        var rows=emptyList<DestinationPlace>()
        override fun getCount()=rows.size
        override fun getItem(i:Int)=rows[i]
        override fun getItemId(i:Int)=i.toLong()
        override fun getView(i:Int,convert:View?,parent:ViewGroup):View {
            val p=rows[i]
            return TextView(activity).apply {text="${p.name}\n${p.detail}";textSize=16f;setTextColor(Color.WHITE)
                setBackgroundColor(Color.rgb(25,43,56));setPadding(14,12,14,12)}
        }
        private val destinationFilter=object:Filter() {
            override fun performFiltering(query:CharSequence?):FilterResults {
                val found=runCatching {index.search(query?.toString()?:"")}.getOrDefault(emptyList())
                return FilterResults().apply {values=found;count=found.size}
            }
            override fun publishResults(query:CharSequence?,result:FilterResults?) {
                if(query?.toString()!=search.text.toString())return
                @Suppress("UNCHECKED_CAST")
                rows=(result?.values as? List<DestinationPlace>)?:emptyList();notifyDataSetChanged()
                if(search.hasFocus()&&search.text.length>=2&&rows.isEmpty())message.text="No mapped match. Try a nearby town or landmark."
            }
            override fun convertResultToString(result:Any?)=(result as? DestinationPlace)?.name?:""
        }
        override fun getFilter()=destinationFilter
    }
    init {
        view.addView(search,LinearLayout.LayoutParams(-1,-2));view.addView(message)
        view.addView(LinearLayout(activity).apply {addView(locate);addView(clear)})
        search.setAdapter(results)
        search.doAfterTextChanged {
            if(!settingText&&editable) {
                // A search is only a draft. Keep the committed route available
                // until a replacement has been prepared and atomically saved.
                revision.incrementAndGet();waiting=null;busy=false
                message.text=if(index.size==0)"Preparing destination search…" else "Choose a destination."
            }
        }
        search.setOnItemClickListener {_,_,i,_->
            val place=results.getItem(i);revision.incrementAndGet();waiting=place;busy=false
            search.clearFocus();search.dismissDropDown()
            (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).hideSoftInputFromWindow(search.windowToken,0)
            requestLocation();tick()
        }
        search.setOnEditorActionListener {_,_,_->search.showDropDown();true}
        reloadIndex()
    }
    fun reloadIndex() {
        message.text="Preparing destination search…"
        worker.execute {
            val result=runCatching {
                val packs=OfflinePack.installed(activity).sortedWith(compareByDescending<OfflinePack>{it.metadata.optInt("places_schema",1)}.thenByDescending {it.id})
                    .distinctBy {it.bounds.joinToString(",")}
                DestinationIndex.combine(packs.map {DestinationIndex.load(it)})
            }
            main.post {if(!closed){val previous=index;index=result.getOrDefault(DestinationIndex(emptyList()));worker.execute {previous.close()}
                if(waiting==null)message.text=if(result.isFailure)"Search unavailable: ${result.exceptionOrNull()?.message}" else if(index.size==0)"Add an offline region in Lab settings to enable search." else "Search towns, landmarks and businesses in your offline region."
                if(search.text.length>=2)results.filter.filter(search.text) {count->
                    if(count>0&&editable&&search.hasFocus()&&search.isAttachedToWindow)search.showDropDown()
                }
            }else result.getOrNull()?.close()}
        }
    }
    fun importPack(uri:Uri) {
        if(!editable||busy)return
        busy=true;message.text="Adding offline region…"
        worker.execute {
            val result=runCatching {OfflinePack.import(activity,requireNotNull(activity.contentResolver.openInputStream(uri)))}
            main.post {if(!closed){busy=false
                if(result.isSuccess){packImported(result.getOrThrow());reloadIndex()} else message.text="Could not add region: ${result.exceptionOrNull()?.message}"
            }}
        }
    }
    fun restore(route:OfflineRoute) {
        if(waiting!=null||search.hasFocus())return
        settingText=true;search.setText(route.destinationLabel.ifBlank {"Saved destination"},false);settingText=false
        message.text="Route ready · %.1f km".format(route.lengthKm)
    }
    fun clearDestination() {
        if(!editable)return
        revision.incrementAndGet();waiting=null;busy=false
        settingText=true;search.setText("",false);settingText=false;search.dismissDropDown()
        routeCleared();message.text="Camera coaching continues without a destination."
    }
    fun setEditable(value:Boolean) {
        if(editable&&!value){search.dismissDropDown();search.clearFocus()
            (activity.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).hideSoftInputFromWindow(search.windowToken,0)}
        editable=value;search.isEnabled=value;clear.isEnabled=value;locate.isEnabled=value
    }
    fun setRunning(value:Boolean) {
        if(running&&!value){revision.incrementAndGet();busy=false}
        running=value
    }
    fun tick() {
        val target=waiting?:return;if(closed||busy||!running)return
        val fix=position()
        val age=fix?.let {android.os.SystemClock.elapsedRealtimeNanos()/1e9-it.elapsedRealtimeNanos/1e9}?:Double.POSITIVE_INFINITY
        if(fix==null||age !in 0.0..5.0||!fix.hasAccuracy()||fix.accuracy>25) {
            message.text="${target.name} selected · Waiting for precise GPS. Your route will be added automatically.";return
        }
        if(!target.pack.contains(fix.latitude,fix.longitude)) {message.text="No offline routing coverage for your current position.";waiting=null;return}
        busy=true;val ticket=revision.get();message.text="Adding route to ${target.name}…"
        worker.execute {
            val prepared=runCatching {
                if(routerPack!=target.pack.id){router?.close();router=OfflineRouter(target.pack,activity.assets.open("navigation-valhalla-config.json").bufferedReader().use {it.readText()});routerPack=target.pack.id}
                val route=router!!.route(fix.latitude,fix.longitude,target.latitude,target.longitude).copy(destinationLabel=target.name)
                val folder=File(activity.filesDir,"navigation").apply {mkdirs()}
                val tmp=File(folder,"destination-$ticket.tmp");tmp.writeText(route.json());Pair(route,tmp)
            }
            main.post {
                val data=prepared.getOrNull()
                if(closed||ticket!=revision.get()||!running){data?.second?.delete();return@post}
                busy=false;waiting=null
                if(data==null){message.text="Could not add route: ${prepared.exceptionOrNull()?.message}";return@post}
                if(!data.second.renameTo(File(activity.filesDir,"navigation/active-route.json"))) {
                    data.second.delete();message.text="Could not save destination. Try again.";return@post
                }
                message.text="Route ready · %.1f km".format(data.first.lengthKm)
                routeSaved()
            }
        }
    }
    override fun close(){closed=true;revision.incrementAndGet();worker.execute {router?.close();index.close()};worker.shutdown();main.removeCallbacksAndMessages(null)}
}
