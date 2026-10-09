$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot 'build\camera-health-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$policy = Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\CameraVisionHealth.java'
$tests = Join-Path $projectRoot 'tests\CameraVisionHealthTest.java'
& javac --release 17 -d $outputDir $policy $tests
if ($LASTEXITCODE -ne 0) { throw 'Camera-health test compilation failed.' }
& java -cp $outputDir ai.learndrive.mobile.CameraVisionHealthTest
if ($LASTEXITCODE -ne 0) { throw 'Camera-health tests failed.' }
