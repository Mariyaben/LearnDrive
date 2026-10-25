package ai.learndrive.mobile.navigation

import android.content.pm.PackageManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import ai.learndrive.mobile.BuildConfig
import com.google.android.gms.common.api.ApiException
import com.google.android.gms.tasks.Tasks
import com.google.android.libraries.places.api.Places
import com.google.android.libraries.places.api.model.AutocompleteSessionToken
import com.google.android.libraries.places.api.model.Place
import com.google.android.libraries.places.api.net.FetchPlaceRequest
import com.google.android.libraries.places.api.net.FindAutocompletePredictionsRequest
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.ExecutionException
import java.util.concurrent.TimeUnit

/** One real autocomplete session; no key, coordinates or Google place data are written to evidence. */
@RunWith(AndroidJUnit4::class)
class GooglePlacesTest {
    @Test fun restrictedAndroidKeyCanSearchAndResolveDestination() {
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue("Configure the local Android key before this network qualification",BuildConfig.GOOGLE_NAVIGATION_AVAILABLE)
        val key=context.packageManager.getApplicationInfo(context.packageName,PackageManager.GET_META_DATA)
            .metaData.getString("com.google.android.geo.API_KEY").orEmpty()
        assertTrue("Android manifest must contain the local configured key",key.isNotBlank())
        if(!Places.isInitialized())Places.initializeWithNewPlacesApiEnabled(context,key)
        val client=Places.createClient(context)
        val token=AutocompleteSessionToken.newInstance()
        val started=android.os.SystemClock.elapsedRealtime()
        try {
            val predictions=Tasks.await(client.findAutocompletePredictions(
                FindAutocompletePredictionsRequest.builder().setQuery("Kochi").setSessionToken(token).build()),30,TimeUnit.SECONDS)
            assertTrue("Real Google destination search must return suggestions",predictions.autocompletePredictions.isNotEmpty())
            val prediction=predictions.autocompletePredictions.first()
            val resolved=Tasks.await(client.fetchPlace(FetchPlaceRequest.builder(prediction.placeId,listOf(Place.Field.LOCATION))
                .setSessionToken(token).build()),30,TimeUnit.SECONDS)
            val location=resolved.place.location
            assertTrue("Selected suggestion must resolve to a usable destination",location!=null&&location.latitude.isFinite()&&location.longitude.isFinite())
            File(context.filesDir,"google-places-verification.json").writeText(JSONObject()
                .put("real_prediction_request_passed",true).put("real_selected_place_request_passed",true)
                .put("autocomplete_session_token_shared",true).put("fields_requested","LOCATION")
                .put("elapsed_ms",android.os.SystemClock.elapsedRealtime()-started)
                .put("contains_api_key",false).put("contains_place_content",false).put("contains_coordinates",false)
                .toString(2))
        } catch(error:ExecutionException) {
            val code=(error.cause as? ApiException)?.statusCode
            fail("Google Places request failed; SDK status code=$code. Check enabled API, Android key restrictions, billing and network.")
        }
    }
}
