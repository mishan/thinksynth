thinksynth
==========

thinksynth builds sounds out of a graph of DSP nodes — oscillators,
filters, envelopes, delays and arithmetic — described in a small
language and rendered in real time. Patches assign those graphs to MIDI
channels, so one instrument can layer several voices.

Every node is a plugin, and the set that ships covers subtractive and
FM synthesis, resonators, waveshaping and a range of filters. New ones
are ordinary shared libraries dropped into the plugin directory.

Building
--------

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
./build/src/thinksynth
```

On Debian or Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build bison flex pkg-config \
    libgtkmm-4.0-dev libsigc++-3.0-dev libasound2-dev libjack-jackd2-dev
```

Useful options:

| | |
|---|---|
| `-DCMAKE_BUILD_TYPE=Debug` | unoptimised, with symbols |
| `-DTHINK_ENABLE_DEBUG=ON` | the tree's own debug logging |
| `-DTHINK_SANITIZE=address,undefined` | applied to libthink and the plugins too, not just the harnesses |
| `-DTHINK_SANITIZE=thread` | needs its own build tree; cannot be combined with the above |

Running an uninstalled build works: thinksynth tries `./plugins` first and then
the directories around its own binary, and `THINK_PLUGIN_PATH` overrides both.

Audio and MIDI
--------------

Audio goes through RtAudio and MIDI through RtMidi, so the same build talks to
ALSA, JACK, CoreAudio, WASAPI, CoreMIDI and WinMM without a per-backend code
path.

```sh
thinksynth -d rtaudio          # the default
thinksynth -d jack             # JACK directly
thinksynth -d none             # no audio device; useful headless
```

The audio API and device are remembered in `thinkrc`, and the preferences
dialogue lists what the machine actually has — RtAudio enumerates devices, so
there is nothing to type.

`thinkrc` lives under the platform's config directory —
`~/.config/thinksynth/thinkrc` on Linux, `~/Library/Application
Support/thinksynth/thinkrc` on macOS, `%LOCALAPPDATA%\thinksynth\thinkrc` on
Windows. A `~/.thinkrc` from an older version is still read if no current one
exists, but is never written back to.

For MIDI, connect an external sequencer or keyboard to thinksynth's input port
using whatever your platform uses for that (`aconnect` or a patchbay on Linux,
Audio MIDI Setup on macOS). Assign a DSP to each channel the MIDI uses and turn
up the amplitudes. The on-screen keyboard works without any of this.

If you want JACK on Linux, start `jackd` before thinksynth — RtAudio will use
the running server.

Documentation
-------------

| | |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | the layers, key classes, threading model, audio and MIDI paths |
| [DSP_FORMAT.md](DSP_FORMAT.md) | the `.dsp` and `.patch` formats, and the rules for writing them |
| [AUDIO.md](AUDIO.md) | the output stage: clamping, gain staging, arg initialisation, the harnesses |
| [NODE_EDITOR.md](NODE_EDITOR.md) | the visual editor's model, behaviour and layout |
| [VISUALIZERS.md](VISUALIZERS.md) | writing a visual module, and how probes work |
| [PORTING.md](PORTING.md) | macOS and Windows: decisions, build system, CI, traps |
| [PACKAGING.md](PACKAGING.md) | the three install layouts, dependency closure, GTK bundling, Flatpak |
| [TODO](TODO) | what is left |
