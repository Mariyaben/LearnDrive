param([string]$ZigPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot 'build\zig'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
if (-not $ZigPath) { $ZigPath = Join-Path $projectRoot '.tools\zig\ziglang\zig.exe' }
if (-not (Test-Path -LiteralPath $ZigPath)) { throw 'Supply -ZigPath or install ziglang under .tools/zig.' }
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot '.tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $outputDir 'cache'
$sources = @('fusion.cpp', 'decision.cpp', 'vision.cpp', 'c_api.cpp', 'SceneState.cpp', 'ReflexEngine.cpp', 'PriorityArbiter.cpp', 'ContextualCognitionEngine.cpp', 'ContextualPolicyEngine.cpp', 'DualBrainEngine.cpp', 'dual_brain_c_api.cpp') | ForEach-Object { Join-Path $projectRoot "core\src\$_" }
$sources += Join-Path $projectRoot 'core\src\perception\camera_calibration.cpp'
$sources += Join-Path $projectRoot 'core\src\perception\metric_estimator.cpp'
$sources += Join-Path $projectRoot 'core\src\perception\supercombo.cpp'
$sources += Join-Path $projectRoot 'core\src\PostDriveSummary.cpp'
$sources += Join-Path $projectRoot 'core\src\navigation\polyline_analyzer.cpp'
$sources += Join-Path $projectRoot 'core\src\coaching\CoachingEngine.cpp'
$sources += Join-Path $projectRoot 'core\src\coaching\TemporalSupport.cpp'
$include = Join-Path $projectRoot 'core\include'
$testExe = Join-Path $outputDir 'core_tests.exe'
& $ZigPath c++ -std=c++17 -O2 '-Wall' '-Wextra' '-Wpedantic' "-I$include" @sources (Join-Path $projectRoot 'tests\core_tests.cpp') -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'Core test compilation failed.' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
$deskExe = Join-Path $outputDir 'learndrive_desk.exe'
& $ZigPath c++ -std=c++17 -O2 "-I$include" @sources (Join-Path $projectRoot 'desktop\main.cpp') -o $deskExe
if ($LASTEXITCODE -ne 0) { throw 'Desktop harness compilation failed.' }
& $deskExe (Join-Path $projectRoot 'scenarios\desk.csv')
if ($LASTEXITCODE -ne 0) { throw 'Desktop scenario failed.' }
foreach ($target in @(@{Name='dual_brain_tests'; Source='tests\dual_brain_tests.cpp'}, @{Name='dual_brain_harness'; Source='desktop\dual_brain_harness.cpp'})) {
    $exe = Join-Path $outputDir ($target.Name + '.exe')
    & $ZigPath c++ -std=c++17 -O2 '-Wall' '-Wextra' '-Wpedantic' "-I$include" @sources (Join-Path $projectRoot $target.Source) -o $exe
    if ($LASTEXITCODE -ne 0) { throw "$($target.Name) compilation failed." }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$($target.Name) failed." }
}
foreach ($target in @(@{Name='test_reflex_suppression'; Source='core\tests\test_reflex_suppression.cpp'},
    @{Name='temporal_support_tests'; Source='tests\temporal_support_tests.cpp'},
    @{Name='coaching_engine_tests'; Source='tests\coaching_engine_tests.cpp'},
    @{Name='test_priority_arbiter'; Source='core\tests\test_priority_arbiter.cpp'},
    @{Name='test_perception_geometry'; Source='core\tests\test_perception_geometry.cpp'})) {
    $exe = Join-Path $outputDir ($target.Name + '.exe')
    & $ZigPath c++ -std=c++17 -O2 '-Wall' '-Wextra' '-Wpedantic' "-I$include" @sources (Join-Path $projectRoot $target.Source) -o $exe
    if ($LASTEXITCODE -ne 0) { throw "$($target.Name) compilation failed." }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$($target.Name) failed." }
}
