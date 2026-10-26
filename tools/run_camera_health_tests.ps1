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
$readiness = Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\SpatialReadiness.java'
$readinessTests = Join-Path $projectRoot 'tests\SpatialReadinessTest.java'
& javac --release 17 -d $outputDir $readiness $readinessTests
if ($LASTEXITCODE -ne 0) { throw 'Spatial-readiness test compilation failed.' }
& java -cp $outputDir ai.learndrive.mobile.SpatialReadinessTest
if ($LASTEXITCODE -ne 0) { throw 'Spatial-readiness tests failed.' }
$setupPolicy = Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\DriveSetup.java'
$setupTests = Join-Path $projectRoot 'tests\DriveSetupTest.java'
& javac --release 17 -d $outputDir $setupPolicy $setupTests
if ($LASTEXITCODE -ne 0) { throw 'Drive-setup test compilation failed.' }
& java -cp $outputDir ai.learndrive.mobile.DriveSetupTest
if ($LASTEXITCODE -ne 0) { throw 'Drive-setup tests failed.' }
$levelStart = Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\LevelReferenceStart.java'
$levelStartTests = Join-Path $projectRoot 'tests\LevelReferenceStartTest.java'
& javac --release 17 -d $outputDir $levelStart $levelStartTests
if ($LASTEXITCODE -ne 0) { throw 'Automatic level-reference start compilation failed.' }
& java -cp $outputDir ai.learndrive.mobile.LevelReferenceStartTest
if ($LASTEXITCODE -ne 0) { throw 'Automatic level-reference start tests failed.' }
$journal = Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\SessionJournal.java'
$journalTests = Join-Path $projectRoot 'tests\SessionJournalTest.java'
& javac --release 17 -d $outputDir $journal $journalTests
if ($LASTEXITCODE -ne 0) { throw 'Session journal compilation failed.' }
& java -cp $outputDir ai.learndrive.mobile.SessionJournalTest $outputDir
if ($LASTEXITCODE -ne 0) { throw 'Session journal tests failed.' }
$capability = Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\VisionCapabilityPolicy.java'
$capabilityTests = Join-Path $projectRoot 'tests\VisionCapabilityPolicyTest.java'
& javac --release 17 -d $outputDir $readiness $capability $capabilityTests
if ($LASTEXITCODE -ne 0) { throw 'Vision capability test compilation failed.' }
& java -cp $outputDir ai.learndrive.mobile.VisionCapabilityPolicyTest
if ($LASTEXITCODE -ne 0) { throw 'Vision capability tests failed.' }
$interruption = Join-Path $projectRoot 'android\app\src\main\java\ai\learndrive\mobile\AudioInterruptionPolicy.java'
$interruptionTests = Join-Path $projectRoot 'tests\AudioInterruptionPolicyTest.java'
& javac --release 17 -d $outputDir $interruption $interruptionTests
if ($LASTEXITCODE -ne 0) { throw 'Audio interruption test compilation failed.' }
& java -cp $outputDir ai.learndrive.mobile.AudioInterruptionPolicyTest
if ($LASTEXITCODE -ne 0) { throw 'Audio interruption tests failed.' }
