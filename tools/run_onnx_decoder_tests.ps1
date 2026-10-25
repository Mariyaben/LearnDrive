param([Parameter(Mandatory=$true)][string]$OrtSdkRoot, [string]$ZigPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot 'build\onnx-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
if (-not $ZigPath) { $ZigPath = Join-Path $projectRoot '.tools\zig\ziglang\zig.exe' }
if (-not (Test-Path -LiteralPath $ZigPath)) { throw 'Supply -ZigPath or install ziglang under .tools/zig.' }
$OrtSdkRoot = (Resolve-Path -LiteralPath $OrtSdkRoot).Path
$ortInclude = Join-Path $OrtSdkRoot 'include'
$ortLibrary = Join-Path $OrtSdkRoot 'lib\onnxruntime.lib'
if (-not (Test-Path -LiteralPath $ortLibrary)) { throw 'Expected Windows x64 ONNX Runtime native SDK (1.22 or newer).' }
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot '.tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $outputDir 'cache'
$fixture = Join-Path $outputDir 'decoder-fixture.onnx'
python (Join-Path $projectRoot 'tests\create_decoder_fixture.py') $fixture
if ($LASTEXITCODE -ne 0) { throw 'Fixture export failed; install Python onnx and numpy.' }
$sources = @('SceneState.cpp', 'ReflexEngine.cpp', 'PriorityArbiter.cpp', 'ContextualCognitionEngine.cpp', 'OnnxLanguageModel.cpp') |
    ForEach-Object { Join-Path $projectRoot "core\src\$_" }
$include = Join-Path $projectRoot 'core\include'
$exe = Join-Path $outputDir 'onnx_decoder_tests.exe'
& $ZigPath c++ -std=c++17 -O2 '-Wall' '-Wextra' '-Wpedantic' '-DLEARNDRIVE_WITH_ORT=1' "-I$include" "-I$ortInclude" @sources (Join-Path $projectRoot 'tests\onnx_decoder_tests.cpp') $ortLibrary -o $exe
if ($LASTEXITCODE -ne 0) { throw 'ONNX decoder test compilation failed.' }
Copy-Item -LiteralPath (Join-Path $OrtSdkRoot 'lib\onnxruntime.dll') -Destination $outputDir -Force
& $exe $fixture
if ($LASTEXITCODE -ne 0) { throw 'ONNX decoder tests failed.' }
