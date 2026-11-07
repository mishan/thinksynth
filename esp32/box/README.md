# thinkbox

`dsp/dxbell.dsp` played on an Espressif ESP32-S3-BOX: eight voices of it,
compiled (`scripts/dspcompile`, docs/KERNELS.md), on one core, through the
BOX's speaker at 32 kHz; on the other core the touchscreen and the IMU,
read 250 times a second and played through the patch's own `controller
box` block by the mapper the desktop's controllers have (`hid/thhMapper`,
`hid/thhBox`).

As `dxbell.dsp` sets it: the screen is a strip of keys, C minor pentatonic
two octaves left to right, louder the lower it's touched. Standing
upright facing you, tipping the top away bends the note up and toward
you down; turning it like a wheel makes the bell brighter or darker.

## Building

With ESP-IDF 6 (`. ~/esp/esp-idf/export.sh`) and thinksynth built in
`build/`:

    esp32/box/generate.sh build     # the voice and its profile, from the patch
    cd esp32/box
    idf.py set-target esp32s3
    idf.py -p /dev/ttyACM0 flash monitor

`generate.sh` runs again whenever the patch changes. The monitor prints
what the controls read twice a second.
