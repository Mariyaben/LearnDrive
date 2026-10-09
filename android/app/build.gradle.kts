plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }
android {
    namespace = "ai.learndrive.mobile"
    compileSdk = 35
    ndkVersion = "28.2.13676358"
    defaultConfig {
        applicationId = "ai.learndrive.mobile"
        minSdk = 28
        targetSdk = 35
        versionCode = 9
        versionName = "0.1.8"
        externalNativeBuild { cmake { cppFlags += "-std=c++17" } }
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }
    externalNativeBuild { cmake { path = file("../../CMakeLists.txt"); version = "3.22.1" } }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    androidResources { noCompress += "onnx" }
    buildTypes { release { isMinifyEnabled = false } }
}
dependencies {
    implementation("androidx.activity:activity-ktx:1.9.3")
    implementation("androidx.camera:camera-camera2:1.4.1")
    implementation("androidx.camera:camera-lifecycle:1.4.1")
    implementation("androidx.camera:camera-view:1.4.1")
    implementation("com.microsoft.onnxruntime:onnxruntime-android:1.22.0")
}

val offlineAssets = listOf("detector.onnx", "road.onnx", "model-manifest.json", "TwinLite-LICENSE.txt", "YOLO-LICENSE.txt") +
    (1..35).map { "audio/prompt-$it.wav" }
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
