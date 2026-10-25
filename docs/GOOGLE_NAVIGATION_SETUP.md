# Google navigation setup for LearnDrive

22 October update: 0.4.6 passes the stationary S24 integration test for real
in-place autocomplete, route preparation, ENROUTE feed, camera continuity,
stop/clear and route retention across Activity recreation without an extra app
route request. No simulator or mock GPS was used. This does not qualify real
driving turn timing, GPS loss/rerouting behavior or sustained heat. The account
setup below remains the development configuration.

Checked against official Google documentation on **19 October 2026**.
Google Places and Navigation are implemented in the **0.4.2 source candidate**.
Both APKs assemble, and the restricted key passes real Places prediction and
selected-place requests on the S24 Ultra. Complete Navigation route/feed/lifecycle
and sustained workload qualification remain **pending**.
The separately verified **0.4.1 APK** is the earlier offline foundation: it has
no internet permission and makes no Google requests. Do not use its checks to
claim the new provider works.

Current account setup: project `learndrive-development-512108` is selected. The
three required services below are enabled, and the development key is stored in
ignored `android/secrets.properties`. On 19 October, Cloud Console confirmed the
saved key restrictions: Android app `ai.learndrive.mobile` with the debug SHA-1
below, and access to only those three APIs. This account configuration is
complete, and native Places requests succeed with this key. Navigation requests,
usage reporting and spending controls still need their own checks. The following account steps document future
environments; the current project/key do not need to be recreated.

## 1. Create your development project

Open [Google Cloud project creation](https://console.cloud.google.com/projectcreate)
with your own Google account. Create a dedicated project, for example
**LearnDrive Development**, and note its **project ID**. Keep this project selected
through the following steps. You must complete account, payment and billing
verification yourself. Link a billing account to this project; Navigation and
Places require enabled billing. Review the account's country and currency before
confirming. [Navigation setup](https://developers.google.com/maps/documentation/navigation/android-sdk/get-api-key),
[Places overview](https://developers.google.com/maps/documentation/places/android-sdk/overview).

## 2. Set spending alerts and request quotas

In **Billing → Budgets & alerts**, create a monthly budget scoped to this project
for an amount you are comfortable spending on the prototype. Add actual-spend
alerts at 50%, 80% and 100%, and check that your email receives them.

**An alerts-only budget does not stop requests or cap spending.** Reporting and
notifications can lag. Google now offers separate spend-cap budgets for supported
services; do not assume Navigation or Places is covered without checking the
specific service support. [Cloud Billing budget behavior](https://docs.cloud.google.com/billing/docs/how-to/budgets).

After enabling the services below, review **Google Maps Platform → Quotas** and
set low testing limits where available. Check each quota's units: a per-minute
limit is not a daily spending limit. Exhausted quotas can make routing/search
unavailable. Navigation charges route requests per destination; Places has
separate search/details SKUs. Review current global or eligible India pricing,
rather than assuming the prototype is entirely free.
[Navigation billing](https://developers.google.com/maps/documentation/navigation/android-sdk/pricing),
[Places billing](https://developers.google.com/maps/documentation/places/android-sdk/usage-and-billing).

## 3. Enable these three services

Open **APIs & Services → Library** in the selected project. Search for and enable:

| Service in Cloud Console | Purpose |
| --- | --- |
| **Navigation SDK** | In-app routing, guidance and maneuver feed |
| **Maps SDK for Android** | Authorization for the bundled Google map |
| **Places API (New)** | Destination predictions and selected-place information |

Google's [Android navigation codelab](https://codelabs.developers.google.com/codelabs/maps-platform/navigation-sdk-101-android)
instructs enabling Navigation and Maps SDK for Android in Cloud. This does not
mean adding two competing map dependencies to the APK. For Places, use the New
API with the [current Places initialization](https://developers.google.com/maps/documentation/places/android-sdk/config).
We do not need separate Directions, Routes, Roads, Geocoding, JavaScript Maps or
Fleet Engine services for this first native SDK integration.

## 4. Create and restrict the Android development key

Go to **APIs & Services → Credentials**. Edit the existing development key
(currently **Maps Platform API Key**). For a new environment, create a key named
**LearnDrive Android Debug** and restrict it immediately:

1. Under **Application restrictions**, choose **Android apps**.
2. Add this application entry:

   ```text
   Package name: ai.learndrive.mobile
   SHA-1: C7:6A:AA:C4:F9:5E:0E:3F:97:D8:C0:5E:BB:9D:3C:2A:3B:6D:A4:8F
   ```

3. In **Select API restrictions** (or **API restrictions → Restrict key** in the
   older layout), select only **Navigation SDK**, **Maps SDK for Android**, and
   **Places API (New)**. Remove any other preselected APIs, then confirm **OK**.
4. Save. If a service is absent, verify it was enabled in this same project.

The fingerprint above identifies the current development signing certificate;
it is not the phone's fingerprint. Release/Play builds will need their own
verified signing certificate restrictions and a separate production key.
[Official key restriction instructions](https://developers.google.com/maps/api-security-best-practices).

Keep the key out of chat, screenshots, source files and commits. A key shipped
inside an Android app is recoverable; package/certificate and API restrictions
are essential. A local properties file helps prevent accidental commits but does
not make a shipped Android key secret.

## 5. Configure the local build

For a new environment, record the **project ID** and complete billing, API
enablement and key restrictions. Enter the key after `MAPS_API_KEY=` in the
ignored local file `android/secrets.properties`; do not send its value in chat.
The current development environment already has this file configured.

The 0.4.2 candidate build reads this property and injects it into
`com.google.android.geo.API_KEY` manifest metadata. BuildConfig records only
whether Google configuration is available, not the key value. A blank/missing
key leaves Google unavailable while preserving the offline provider. No
service-account JSON file is needed for this Android SDK setup.
[Google's local key setup](https://developers.google.com/maps/documentation/navigation/android-sdk/android-studio-setup).

The candidate keeps destination entry, map, camera and coach in the same
dashboard. Google handles map/search/routing requests and navigation location;
camera frames, detector/road/Supercombo inference, motion rules and PCM voice
stay local. Ordinary diagnostic exports exclude raw GPS coordinates and key
values. Offline navigation remains an explicitly selected local fallback.
Google's SDK does not provide full offline navigation.
[Navigation FAQ](https://developers.google.com/maps/documentation/navigation/android-sdk/faq).

## Candidate toolchain and engineering checks

The source pins are JDK **17**, AGP **8.13.2**, SHA-256-checked Gradle **8.13**,
Kotlin **2.2.10**, compile/target API **36** and minimum API **28**. Dependencies
are Navigation **7.6.1**, Places **5.1.1**, NIO desugaring **2.0.3** and Datastore
core **1.2.1**. See [BUILD.md](BUILD.md) for the wrapper checksum and build commands.

The app retains its Kotlin 2.2 compiler while AGP 8.13.2 provides the newer R8
needed by the existing Valhalla dependency's Kotlin 2.3 metadata on the NIO
desugaring path. The earlier assembly attempt failed there under the older
toolchain; the upgraded candidate still needs a complete assembly pass. Do not
replace the pinned Navigation branch with an unqualified latest version.
[Release notes](https://developers.google.com/maps/documentation/navigation/android-sdk/release-notes),
[Kotlin/toolchain compatibility](https://developers.google.com/maps/documentation/navigation/android-sdk/kotlin-compatibility-migration).

Source integration and device qualification have separate checkpoints:

- **Source configured:** Navigation includes Google's map implementation; the
  build excludes a separate `com.google.android.gms:play-services-maps` copy.
  The NIO desugaring dependency is pinned. Datastore core 1.2.1 replaces the
  older transitive native library for the 16 KiB alignment check; final APK
  alignment still requires verification.
  [Android integration](https://developers.google.com/maps/documentation/navigation/android-sdk/android-studio-setup).
- **Source implemented:** Places initializes with
  `Places.initializeWithNewPlacesApiEnabled`, uses autocomplete session tokens
  and resolves only the selected place location. Search runs asynchronously
  after typing. Actual authorized predictions/details requests remain pending.
  [Places initialization](https://developers.google.com/maps/documentation/places/android-sdk/config),
  [New session tokens](https://developers.google.com/maps/documentation/places/android-sdk/place-session-tokens).
- **Source implemented:** Google voice is silent. Structured maneuver estimates
  enter LearnDrive's shared coach with separate measured-GPS, identity, freshness
  and speech-timing gates. Google data is not converted to Valhalla shape
  indices. Generic left/right preparation is the current spoken subset;
  unsupported maneuvers stay visual and Google-derived bend voice is disabled.
- **Device checks pending:** Present the unmodified Navigation SDK terms and required independent-judgment,
  actual-road-condition and toll/cost notices on first use. Respect rejection.
  Keep Google's attribution and essential map controls visible, provide required
  legal notices, and keep Google content distinct from local camera overlays.
  Do not persist Google road names/speed limits outside the session.
  [Navigation policies](https://developers.google.com/maps/documentation/navigation/android-sdk/policies),
  [terms acceptance API](https://developers.google.com/maps/documentation/navigation/android-sdk/reference/com/google/android/libraries/navigation/NavigationApi).
- **Device checks pending:** Verify Google Play services, location permission,
  map authorization/rendering, real destination search/routing, the live maneuver
  feed, rerouting, network loss, restricted-key rejection and usage reporting on
  the S24 Ultra / Android 16. Test Google → Offline → Google recovery, activity
  pause/recreation, stale GPS/feed abstention and audible cancellation of
  obsolete turn cues while urgent camera speech remains intact.
  [SDK requirements](https://developers.google.com/maps/documentation/navigation/android-sdk/setup-overview).

Current native coverage passes 103 navigation/provider checks and the shared
camera/provider coordinator test. It does not establish successful Android
assembly, Google account authorization, real-road usefulness or sustained
combined performance. The [candidate review](reviews/2026-10-19-google-navigation.md)
tracks those remaining qualification steps.
