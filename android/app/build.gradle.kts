import java.util.Properties

plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }

// Local-only credentials are copied into manifest metadata, never logged or
// duplicated in BuildConfig. Missing credentials keep the offline mode usable.
val mapsApiKey = Properties().apply {
    val secretsFile = rootProject.file("secrets.properties")
    if (secretsFile.isFile) secretsFile.inputStream().use { load(it) }
}.getProperty("MAPS_API_KEY", "").trim()

android {
    namespace = "ai.learndrive.mobile"
    compileSdk = 36
    ndkVersion = "28.2.13676358"
    defaultConfig {
        applicationId = "ai.learndrive.mobile"
        minSdk = 28
        targetSdk = 36
        versionCode = 29
        versionName = "0.5.0"
        manifestPlaceholders["MAPS_API_KEY"] = mapsApiKey
        buildConfigField("boolean", "GOOGLE_NAVIGATION_AVAILABLE", mapsApiKey.isNotBlank().toString())
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild { cmake { cppFlags += "-std=c++17"; arguments += "-DLEARNDRIVE_WITH_SPATIAL_ORT=ON" } }
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }
    externalNativeBuild { cmake { path = file("../../CMakeLists.txt"); version = "3.22.1" } }
    buildFeatures { buildConfig = true }
    compileOptions {
        isCoreLibraryDesugaringEnabled = true
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    androidResources { noCompress += "onnx" }
    packaging { jniLibs { pickFirsts += "**/libonnxruntime.so" } }
    buildTypes { release { isMinifyEnabled = false } }
}

// Navigation already bundles the Maps SDK. Places must use that same copy.
configurations.configureEach {
    exclude(group = "com.google.android.gms", module = "play-services-maps")
}

dependencies {
    // Pin the final Kotlin 2.2-compatible Navigation branch and matching NIO
    // desugaring instead of forcing the Kotlin 2.3 / AGP 9 upgrade from SDK 8.
    implementation("com.google.android.libraries.navigation:navigation:7.6.1")
    implementation("com.google.android.libraries.places:places:5.1.1")
    // Navigation's DataStore dependency otherwise resolves to 1.2.0, whose
    // native shared counter lacks the GNU_RELRO padding needed for 16 KiB pages.
    implementation("androidx.datastore:datastore-core:1.2.1")
    coreLibraryDesugaring("com.android.tools:desugar_jdk_libs_nio:2.0.3")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test:core:1.6.1")
    implementation("io.github.rallista:valhalla-mobile:0.6.4")
    implementation("org.maplibre.gl:android-sdk:13.6.1")
    implementation("androidx.activity:activity-ktx:1.9.3")
    implementation("androidx.camera:camera-camera2:1.4.1")
    implementation("androidx.camera:camera-lifecycle:1.4.1")
    implementation("androidx.camera:camera-view:1.4.1")
    implementation("com.microsoft.onnxruntime:onnxruntime-android:1.22.0")
}

val offlineAssets = listOf("navigation-valhalla-config.json", "supercombo.onnx", "supercombo-manifest.json", "COMMA-LICENSE.txt", "detector.onnx", "road.onnx", "model-manifest.json", "TwinLite-LICENSE.txt", "YOLO-LICENSE.txt") +
    (1..55).map { "audio/prompt-$it.wav" }
val verifyOfflineAssets = tasks.register("verifyOfflineAssets") {
    inputs.files(offlineAssets.map { file("src/main/assets/$it") })
    doLast {
        offlineAssets.forEach { name ->
            val asset = file("src/main/assets/$name")
            check(asset.isFile && asset.length() > 0) { "Missing offline asset $name. See tools/export_android_models.py and tools/render_android_prompts.ps1." }
        }
    }
}
tasks.named("preBuild") { dependsOn(verifyOfflineAssets) }
