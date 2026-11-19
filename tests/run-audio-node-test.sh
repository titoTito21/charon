#!/bin/sh
set -eu
pcm=${1:?path to 22050Hz mono signed16 PCM required}
cd "$(dirname "$0")/.."
mkdir -p bin
c++ -std=c++20 -Wall -Wextra -Werror src/HaikuAudioNode.cpp tests/AudioNodeTest.cpp \
    -lbe -lmedia -o bin/.AudioNodeTest.new
mv -f bin/.AudioNodeTest.new bin/AudioNodeTest
timeout 15 bin/AudioNodeTest "$pcm"
