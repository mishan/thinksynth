#!/bin/sh
g++ main.cpp OSSAudio.cpp Wav.cpp endian.cpp AudioBuffer.cpp -g -Wall -o oss_wavplay
