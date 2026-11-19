#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p bin
c++ -std=c++20 -Wall -Wextra -Werror -DHAIKU_AUDIO_NODE_TESTING src/HaikuAudioNode.cpp tests/AudioClockTest.cpp \
    -lbe -lmedia -o bin/.AudioClockTest.new
mv -f bin/.AudioClockTest.new bin/AudioClockTest
timeout 60 bin/AudioClockTest
