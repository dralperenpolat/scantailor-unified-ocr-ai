#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/stou-tools/python/bin:$PATH"
cmake -S native/scantailor-experimental -B native/release-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSTOU_GUI_OUTPUT_NAME=scantailor-ocr-unified \
  -DSTOU_CLI_OUTPUT_NAME=scantailor-experimental-cli -DENABLE_OPENCL=ON -DENABLE_OPENGL=OFF \
  -DCOMPILER_FLAGS_OVERRIDDEN=ON -DCMAKE_CXX_FLAGS='-Wall -Werror=return-type -Wno-deprecated-declarations'
cmake --build native/release-linux --target scantailor-experimental scantailor-experimental-cli opencl_plugin -j2
