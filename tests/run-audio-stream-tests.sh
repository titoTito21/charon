#!/bin/sh
set -eu
pcm=${1:?absolute path to 22050Hz mono signed16 PCM required}
cd "$(dirname "$0")/.."
mkdir -p bin
c++ -std=c++20 -Wall -Wextra -Werror src/HaikuAudioNode.cpp src/HaikuPcmQueue.cpp \
    src/HaikuAudioStream.cpp tests/AudioStreamLiveTest.cpp -lbe -lmedia \
    -o bin/.AudioStreamLiveTest.new
mv -f bin/.AudioStreamLiveTest.new bin/AudioStreamLiveTest
timeout 15 bin/AudioStreamLiveTest "$pcm" pause
timeout 15 bin/AudioStreamLiveTest "$pcm" cancel
