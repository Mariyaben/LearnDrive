param([string]$ZigPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot 'build\zig'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
if (-not $ZigPath) { $ZigPath = Join-Path $projectRoot '.tools\zig\ziglang\zig.exe' }
if (-not (Test-Path -LiteralPath $ZigPath)) { throw 'Supply -ZigPath or install ziglang under .tools/zig.' }
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot '.tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $outputDir 'cache'
$sources = @('fusion.cpp', 'decision.cpp', 'vision.cpp', 'c_api.cpp') | ForEach-Object { Join-Path $projectRoot "core\src\$_" }
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
