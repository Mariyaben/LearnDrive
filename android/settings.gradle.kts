pluginManagement { repositories { google(); mavenCentral(); gradlePluginPortal() } }
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        // Optional build-time mirror; model export environments can prefetch ORT.
        val workspaceCache=file("../.tools/maven-repo")
        if(workspaceCache.isDirectory)maven {url=uri(workspaceCache);content {includeGroup("com.microsoft.onnxruntime")}}
        google {content {excludeGroup("com.microsoft.onnxruntime")}}
        mavenCentral()
    }
}
rootProject.name = "LearnDrive"
include(":app")
