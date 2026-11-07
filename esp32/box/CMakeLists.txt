# thinksynth on an Espressif ESP32-S3-BOX: dsp/dxbell.dsp compiled to a voice
# (scripts/dspcompile), eight of them, played from the BOX's touchscreen and
# IMU through the patch's own controller block. See README.md.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(thinkbox)
