#!/usr/bin/env bash
set -euo pipefail
sudo apt-get update
sudo apt-get install -y --no-install-recommends build-essential ninja-build qtbase5-dev qttools5-dev qttools5-dev-tools libqt5svg5-dev libeigen3-dev libboost-dev libtiff-dev libjpeg-dev zlib1g-dev libpng-dev ocl-icd-opencl-dev libxkbcommon-x11-0 libxcb-xinerama0 libxcb-cursor0 fonts-dejavu-core curl ca-certificates patchelf file
mkdir -p "$HOME/stou-tools"
cd "$HOME/stou-tools"
curl --fail --location --retry 3 -o python.tar.gz 'https://github.com/astral-sh/python-build-standalone/releases/download/20261013/cpython-3.12.15%2B20261013-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz'
tar -xzf python.tar.gz
python/bin/python3 -m pip install 'cmake==3.31.10'
echo 'Linux build environment ready.'
