#!/bin/bash
# Build Goblet and open it.
#   ./build.sh          build (Release) and launch
#   ./build.sh debug    build a Debug version and launch
#   ./build.sh xcode    generate an Xcode project in build-xcode/ and open it
#   ./build.sh tests    build and run the measurement program (report in Measurements/)
#                       add --quick for a short run
set -e
cd "$(dirname "$0")"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

if [ "$1" = "xcode" ]; then
    cmake -B build-xcode -G Xcode
    open build-xcode/Goblet.xcodeproj
    exit 0
fi

if [ "$1" = "tests" ]; then
    shift
    JUCE_ARG=""
    [ -d build/_deps/juce-src ] && JUCE_ARG="-DGOBLET_JUCE_PATH=$(pwd)/build/_deps/juce-src"
    cmake -B build-tests -DCMAKE_BUILD_TYPE=Release -DGOBLET_BUILD_TESTS=ON $JUCE_ARG
    cmake --build build-tests --config Release --target GobletTests -j "$JOBS"
    ./build-tests/GobletTests_artefacts/Release/GobletTests --out Measurements "$@"
    echo "Report: Measurements/report.md"
    exit 0
fi

CONFIG=Release
[ "$1" = "debug" ] && CONFIG=Debug

cmake -B build -DCMAKE_BUILD_TYPE=$CONFIG
cmake --build build --config $CONFIG -j "$JOBS"

APP="build/Goblet_artefacts/$CONFIG/Goblet.app"
if [ -d "$APP" ]; then
    echo "Built: $APP"
    open "$APP"
fi
