#!/bin/sh
g++ main.cpp OSSAudio.cpp Wav.cpp endian.cpp AudioBuffer.cpp Exception.cpp -g -Wall -o oss_wavplay
