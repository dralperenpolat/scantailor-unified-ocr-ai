param([string]$Prefix = "$PSScriptRoot/../.build-tools/msys64/ucrt64", [int]$Jobs = 2)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot/..").Path
$Prefix = (Resolve-Path -LiteralPath $Prefix).Path
$env:PATH = "$Prefix/bin;$env:PATH"
& "$Prefix/bin/cmake.exe" -S "$repo/native/scantailor-experimental" -B "$repo/native/release043" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTOU_GUI_OUTPUT_NAME=scantailor-ocr-unified -DSTOU_CLI_OUTPUT_NAME=scantailor-experimental-cli -DENABLE_OPENCL=ON -DENABLE_OPENGL=OFF -DCOMPILER_FLAGS_OVERRIDDEN=ON '-DCMAKE_CXX_FLAGS=-Wall -Werror=return-type -Wno-deprecated-declarations'
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
& "$Prefix/bin/cmake.exe" --build "$repo/native/release043" --target scantailor-experimental scantailor-experimental-cli opencl_plugin -j $Jobs
if ($LASTEXITCODE) { throw 'Native build failed' }
