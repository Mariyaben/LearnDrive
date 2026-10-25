$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot 'build\road-coaching-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$sources = @(
    (Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\RoadCoachingStatus.java'),
    (Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\SpatialReadiness.java'),
    (Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\CameraVisionHealth.java'),
    (Join-Path $projectRoot 'tests\RoadCoachingStatusTest.java'),
    (Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\PracticeSessionReview.java'),
    (Join-Path $projectRoot 'tests\PracticeSessionReviewTest.java')
)
& javac --release 17 -d $outputDir @sources
if ($LASTEXITCODE -ne 0) { throw 'Road coaching status test compilation failed.' }
& java -cp $outputDir ai.learndrive.mobile.RoadCoachingStatusTest
if ($LASTEXITCODE -ne 0) { throw 'Road coaching status tests failed.' }
& java -cp $outputDir ai.learndrive.mobile.PracticeSessionReviewTest
if ($LASTEXITCODE -ne 0) { throw 'Practice session review tests failed.' }
