#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p bin
sh tools/atomic-build.sh bin/libnvda-haiku-audio.so c++ -std=c++20 -Wall -Wextra -Werror -fPIC -shared \
    src/HaikuAudioNode.cpp src/HaikuPcmQueue.cpp src/HaikuAudioStream.cpp \
    src/HaikuSilencePlayer.cpp src/HaikuAudioApi.cpp -lbe -lmedia
