package ai.learndrive.mobile.navigation.google

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import android.widget.*
import androidx.core.widget.doAfterTextChanged
import com.google.android.gms.common.api.ApiException
import com.google.android.gms.maps.model.LatLng
import com.google.android.libraries.places.api.model.AutocompletePrediction
import com.google.android.libraries.places.api.model.AutocompleteSessionToken
import com.google.android.libraries.places.api.model.Place
import com.google.android.libraries.places.api.net.FetchPlaceRequest
import com.google.android.libraries.places.api.net.FindAutocompletePredictionsRequest
import com.google.android.libraries.places.api.net.PlacesClient
import ai.learndrive.mobile.ui.CoachStyle

/** In-place asynchronous autocomplete; no request happens until the user types. Main-thread API. */
internal class GoogleDestinationSearch(private val activity:Activity,
    private val selected:(String,LatLng)->Unit,private val cleared:()->Unit):AutoCloseable {
    private val main=Handler(Looper.getMainLooper())
    private var client:PlacesClient?=null
    private var session:AutocompleteSessionToken?=null
    private var revision=0L
    private var editable=true
    private var settingText=false
    private var closed=false
    private var keyboardVisible=false
    private var popupBeside=false
    private var pending:Runnable?=null
    private lateinit var predictionPopup:PopupWindow
    val view=LinearLayout(activity).apply {orientation=LinearLayout.VERTICAL;setPadding(0,dp(12),0,dp(4))}
    val search=object:AutoCompleteTextView(activity) {
        override fun showDropDown() {showPredictions()}
        override fun dismissDropDown() {
            if(::predictionPopup.isInitialized)predictionPopup.dismiss()
            super.dismissDropDown()
        }
        override fun isPopupShowing()=::predictionPopup.isInitialized&&predictionPopup.isShowing
    }.apply {
        tag="google-destination-search";hint="Where to? Search Google Maps";contentDescription="Google Maps destination"
        setSingleLine();threshold=2;textSize=17f;setTextColor(CoachStyle.text);setHintTextColor(CoachStyle.muted)
        imeOptions=android.view.inputmethod.EditorInfo.IME_ACTION_SEARCH or android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI or android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN
        inputType=android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_CAP_WORDS
        dropDownHeight=(180*resources.displayMetrics.density).toInt();isEnabled=false
        background=CoachStyle.panel(activity,CoachStyle.ink,16)
        minimumHeight=dp(52);setPadding(dp(14),0,dp(10),0)
        setCompoundDrawablesRelativeWithIntrinsicBounds(CoachStyle.icon(activity,"search",CoachStyle.accent),null,null,null)
        compoundDrawablePadding=dp(10)
    }
    private val message=TextView(activity).apply {
        tag="google-destination-status";setTextColor(CoachStyle.muted);textSize=12f;setPadding(dp(2),dp(10),dp(2),dp(4))
        text="Find a town, landmark or practice destination."
    }
    private val inlineAttribution=attribution()
    private val clear=CoachStyle.button(activity,"Clear").apply {isEnabled=false;contentDescription="Clear destination";setPadding(dp(8),0,dp(8),0);setOnClickListener {clear()}}
    private val rows=object:BaseAdapter(),Filterable {
        var items=emptyList<AutocompletePrediction>()
        override fun getCount()=items.size
        override fun getItem(position:Int)=items[position]
        override fun getItemId(position:Int)=position.toLong()
        override fun getView(position:Int,convertView:View?,parent:ViewGroup):View {
            val item=items[position]
            val row=(convertView as? LinearLayout)?:LinearLayout(activity).apply {
                orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER_VERTICAL;minimumHeight=dp(62)
                setPadding(dp(16),dp(9),dp(16),dp(9));setBackgroundColor(CoachStyle.raised)
                addView(CoachStyle.label(activity,"",15f).apply {tag="prediction-title";maxLines=1;ellipsize=android.text.TextUtils.TruncateAt.END;typeface=Typeface.create("sans-serif-medium",0)})
                addView(CoachStyle.label(activity,"",12f,CoachStyle.muted).apply {tag="prediction-detail";maxLines=1;ellipsize=android.text.TextUtils.TruncateAt.END},LinearLayout.LayoutParams(-1,-2).apply {topMargin=dp(4)})
            }
            row.findViewWithTag<TextView>("prediction-title").text=item.getPrimaryText(null)
            row.findViewWithTag<TextView>("prediction-detail").text=item.getSecondaryText(null)
            return row
        }
        private val noop=object:Filter() {
            override fun performFiltering(constraint:CharSequence?)=FilterResults().apply {count=items.size}
            override fun publishResults(constraint:CharSequence?,results:FilterResults?)=Unit
            override fun convertResultToString(result:Any?)=(result as? AutocompletePrediction)?.getPrimaryText(null)?:""
        }
        override fun getFilter()=noop
    }
    init {
        view.addView(LinearLayout(activity).apply {
            gravity=Gravity.CENTER_VERTICAL;addView(search,LinearLayout.LayoutParams(0,dp(52),1f))
            addView(clear,LinearLayout.LayoutParams(dp(56),dp(48)).apply {marginStart=dp(6)})
        })
        view.addView(message)
        view.addView(inlineAttribution,LinearLayout.LayoutParams(-1,-2))
        val list=ListView(activity).apply {
            tag="google-destination-predictions";adapter=rows;dividerHeight=dp(1)
            divider=ColorDrawable(Color.argb(25,255,255,255));isVerticalScrollBarEnabled=false
            setOnItemClickListener {parent,item,position,id->
                search.onItemClickListener?.onItemClick(parent,item,position,id)
            }
        }
        val popupContent=LinearLayout(activity).apply {
            tag="google-destination-popup";orientation=LinearLayout.VERTICAL
            background=CoachStyle.panel(activity,CoachStyle.raised,16);clipToOutline=true
            addView(list,LinearLayout.LayoutParams(-1,0,1f))
            // Fixed outside the scrolling list: attribution stays visible while browsing results.
            addView(attribution(),LinearLayout.LayoutParams(-1,dp(32)))
        }
        predictionPopup=PopupWindow(popupContent,-2,-2,false).apply {
            isOutsideTouchable=true;setBackgroundDrawable(CoachStyle.panel(activity,CoachStyle.raised,16))
            elevation=dp(6).toFloat();inputMethodMode=PopupWindow.INPUT_METHOD_NEEDED
        }
        search.setAdapter(rows)
        search.doAfterTextChanged {if(!settingText&&editable)queueQuery(it?.toString().orEmpty())}
        search.setOnItemClickListener {_,_,position,_->rows.items.getOrNull(position)?.let(::resolve)}
    }
    fun attach(places:PlacesClient) {client=places;setEditable(editable)}
    fun setEditable(value:Boolean) {
        val enabled=value&&client!=null&&!closed
        if(editable==value&&search.isEnabled==enabled&&clear.isEnabled==enabled)return
        editable=value;search.isEnabled=enabled;clear.isEnabled=enabled
        if(!value){cancelDraft();search.dismissDropDown();search.clearFocus();hideKeyboard()}
    }
    fun status(text:String) {message.text=text}
    fun restoreLabel(label:String) {
        settingText=true;search.setText(label,false);settingText=false
    }
    fun requestFocus() {if(search.isEnabled){search.requestFocus();activity.getSystemService(InputMethodManager::class.java)?.showSoftInput(search,InputMethodManager.SHOW_IMPLICIT)}}
    fun setKeyboardVisible(visible:Boolean) {
        if(keyboardVisible==visible)return
        keyboardVisible=visible
        view.setPadding(0,if(visible)0 else dp(12),0,if(visible)0 else dp(4))
        message.maxLines=if(visible)1 else Int.MAX_VALUE
        message.ellipsize=if(visible)android.text.TextUtils.TruncateAt.END else null
        message.setPadding(dp(2),if(visible)dp(4) else dp(10),dp(2),if(visible)0 else dp(4))
        inlineAttribution.visibility=if(visible)View.GONE else View.VISIBLE
        // Result attribution remains fixed in the popup, including beside the keyboard.
        predictionPopup.dismiss()
        search.post {if(search.hasFocus())showPredictions()}
    }
    private fun attribution()=TextView(activity).apply {
        text="Google Maps";contentDescription="Google Maps";tag="google-search-attribution"
        textSize=12f;setTextColor(CoachStyle.text);typeface=Typeface.create("sans-serif",Typeface.NORMAL)
        setSingleLine();gravity=Gravity.END or Gravity.CENTER_VERTICAL;setPadding(dp(10),dp(4),dp(10),dp(4))
        isClickable=false;isFocusable=false
    }
    private fun dp(value:Int)=(value*activity.resources.displayMetrics.density).toInt()
    private fun showPredictions() {
        if(!::predictionPopup.isInitialized||closed||!editable||rows.items.isEmpty()||!search.isShown||search.width<=0)return
        val location=IntArray(2);search.getLocationOnScreen(location)
        val beside=keyboardVisible&&activity.resources.configuration.orientation==android.content.res.Configuration.ORIENTATION_LANDSCAPE&&location[0]>dp(200)
        if(predictionPopup.isShowing&&popupBeside!=beside)predictionPopup.dismiss()
        popupBeside=beside
        if(android.os.Build.VERSION.SDK_INT>=29)predictionPopup.setIsLaidOutInScreen(beside)
        if(beside) {
            // Landscape keyboards leave little height below an input. Use the space beside
            // it for selectable results, keeping the search field and attribution unobscured.
            val frame=android.graphics.Rect();search.getWindowVisibleDisplayFrame(frame)
            val content=activity.findViewById<ViewGroup>(android.R.id.content).getChildAt(0)
            val origin=IntArray(2);content.getLocationOnScreen(origin)
            val x=maxOf(frame.left,origin[0]+content.paddingLeft)
            val y=maxOf(frame.top,location[1])
            val width=location[0]-dp(28)-x
            val height=minOf(dp(224),frame.bottom-y-dp(8))
            if(width<dp(180)||height<dp(96))return
            if(predictionPopup.isShowing)predictionPopup.update(x,y,width,height)
            else {predictionPopup.width=width;predictionPopup.height=height;predictionPopup.showAtLocation(activity.window.decorView,Gravity.TOP or Gravity.START,x,y)}
            return
        }
        val height=minOf(dp(224),predictionPopup.getMaxAvailableHeight(search))
        if(height<dp(96))return
        if(predictionPopup.isShowing)predictionPopup.update(search,0,0,search.width,height)
        else {predictionPopup.width=search.width;predictionPopup.height=height;predictionPopup.showAsDropDown(search)}
    }
    private fun queueQuery(text:String) {
        cancelPending();val ticket=++revision
        if(closed||client==null||!editable)return
        if(text.trim().length<2){rows.items=emptyList();rows.notifyDataSetChanged();search.dismissDropDown();return}
        if(session==null)session=AutocompleteSessionToken.newInstance()
        val token=session!!
        pending=Runnable {
            val places=client?:return@Runnable
            val request=FindAutocompletePredictionsRequest.builder().setQuery(text.take(160)).setSessionToken(token).build()
            message.text="Searching Google Maps…"
            places.findAutocompletePredictions(request).addOnSuccessListener {response->
                if(closed||ticket!=revision||!editable||search.text.toString()!=text)return@addOnSuccessListener
                rows.items=response.autocompletePredictions.take(10);rows.notifyDataSetChanged()
                message.text=if(rows.items.isEmpty())"No match. Try a nearby landmark or town." else "Choose a destination"
                if(rows.items.isNotEmpty()&&search.hasFocus())search.showDropDown()
            }.addOnFailureListener {error->
                if(closed||ticket!=revision)return@addOnFailureListener
                rows.items=emptyList();rows.notifyDataSetChanged();search.dismissDropDown()
                message.text=errorText("Google search",error)
            }
        }.also {main.postDelayed(it,300)}
    }
    private fun resolve(prediction:AutocompletePrediction) {
        val places=client?:return
        if(!editable||closed)return
        cancelPending();val ticket=++revision;val token=session?:AutocompleteSessionToken.newInstance();session=null
        val label=prediction.getPrimaryText(null).toString().take(160)
        settingText=true;search.setText(label,false);settingText=false;search.dismissDropDown();search.clearFocus();hideKeyboard()
        rows.items=emptyList();rows.notifyDataSetChanged();message.text="Finding destination…"
        val request=FetchPlaceRequest.builder(prediction.placeId,listOf(Place.Field.LOCATION)).setSessionToken(token).build()
        places.fetchPlace(request).addOnSuccessListener {response->
            if(closed||ticket!=revision||!editable)return@addOnSuccessListener
            val location=response.place.location
            if(location==null||!location.latitude.isFinite()||!location.longitude.isFinite()) {
                message.text="This destination has no usable location. Choose another result.";return@addOnSuccessListener
            }
            selected(label,location)
        }.addOnFailureListener {error->if(!closed&&ticket==revision)message.text=errorText("Destination lookup",error)}
    }
    private fun cancelPending() {pending?.let(main::removeCallbacks);pending=null}
    private fun cancelDraft() {++revision;cancelPending();session=null;rows.items=emptyList();rows.notifyDataSetChanged()}
    private fun hideKeyboard() {activity.getSystemService(InputMethodManager::class.java)?.hideSoftInputFromWindow(search.windowToken,0)}
    fun clear() {
        if(!editable||closed)return
        cancelDraft();settingText=true;search.setText("",false);settingText=false
        search.dismissDropDown();message.text="Search for a destination";cleared()
    }
    override fun close() {closed=true;cancelDraft();search.dismissDropDown();client=null}
    companion object {
        fun errorText(operation:String,error:Exception):String {
            val code=(error as? ApiException)?.statusCode
            return "$operation unavailable${code?.let {" (code $it)"}.orEmpty()}. Check the connection and Google project access."
        }
    }
}
