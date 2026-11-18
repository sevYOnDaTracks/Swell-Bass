#!/bin/sh
# Compile Swell Bass pour macOS (AU + VST3 + Standalone, universel arm64/x86_64)
set -eu
cd "$(dirname "$0")"
[ -n "$(ls Assets/bank/*.flac 2>/dev/null)" ] || ./Tools/make-bank.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build --parallel 6 --target SwellBass_AU SwellBass_VST3 SwellBass_Standalone
