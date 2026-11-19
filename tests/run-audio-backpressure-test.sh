#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sh tools/atomic-build.sh bin/AudioNodeBackpressureTest c++ -std=c++20 \
    -Wall -Wextra -Werror -DHAIKU_AUDIO_NODE_TESTING \
    src/HaikuAudioNode.cpp src/HaikuPcmQueue.cpp src/HaikuAudioStream.cpp \
    tests/AudioNodeBackpressureTest.cpp -lbe -lmedia
timeout 15 bin/AudioNodeBackpressureTest
