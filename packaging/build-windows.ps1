param([string]$Prefix = "$PSScriptRoot/../.build-tools/msys64/ucrt64", [int]$Jobs = 2, [string]$BuildDir = "$PSScriptRoot/../native/release060")
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot/..").Path
$Prefix = (Resolve-Path -LiteralPath $Prefix).Path
$env:PATH = "$Prefix/bin;$env:PATH"
& "$Prefix/bin/cmake.exe" -S "$repo/native/scantailor-experimental" -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTOU_GUI_OUTPUT_NAME=scantailor-ocr-unified -DSTOU_CLI_OUTPUT_NAME=scantailor-experimental-cli -DENABLE_OPENCL=ON -DENABLE_OPENGL=OFF -DCOMPILER_FLAGS_OVERRIDDEN=ON '-DCMAKE_CXX_FLAGS=-Wall -Werror=return-type -Wno-deprecated-declarations' "-DLIBJPEG_DLL=$Prefix/bin/libjpeg-8.dll" "-DLIBTIFF_DLL=$Prefix/bin/libtiff-6.dll" "-DZLIB_DLL=$Prefix/bin/zlib1.dll"
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
& "$Prefix/bin/cmake.exe" --build $BuildDir --target scantailor-experimental scantailor-experimental-cli opencl_plugin -j $Jobs
if ($LASTEXITCODE) { throw 'Native build failed' }
