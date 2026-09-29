# thinksynth as an audio plugin

`plugin/` builds `dsp/juno.dsp` as an instrument plugin a DAW can load: a
CLAP, a VST3 and an LV2, from one source, through
[DPF](https://github.com/DISTRHO/DPF). The graph is compiled in, and so is
the engine (`think_embedded`), so a plugin is one file or bundle with
nothing beside it.

## Building

```sh
cmake -S . -B build -DTHINK_BUILD_PLUGIN=ON
cmake --build build
```

The first configure fetches DPF (pinned to a commit), and on Linux the two
validators the tests run. The bundles land in `build/bin/`:

| Format | Path | Where a host looks on Linux |
|---|---|---|
| CLAP | `build/bin/thinksynth-juno.clap` | `~/.clap/` |
| VST3 | `build/bin/thinksynth-juno.vst3/` | `~/.vst3/` |
| LV2 | `build/bin/thinksynth-juno.lv2/` | `~/.lv2/` |

Symlinks are the easy way to keep a host pointed at the latest build:

```sh
mkdir -p ~/.clap ~/.vst3 ~/.lv2
ln -sfn "$PWD/build/bin/thinksynth-juno.clap" ~/.clap/
ln -sfn "$PWD/build/bin/thinksynth-juno.vst3" ~/.vst3/
ln -sfn "$PWD/build/bin/thinksynth-juno.lv2"  ~/.lv2/
```

The plugin links the system's `libsigc++-3.0` and nothing else outside the
C and C++ runtimes.

## Trying it in Reaper

Reaper runs on Linux and loads all three formats; its evaluation is
unrestricted.

1. Put the bundles where Reaper looks (the symlinks above), start Reaper,
   and in *Options → Preferences → Plug-ins → VST* (and *CLAP*) press
   *Re-scan*.
2. *Track → Insert virtual instrument on new track*, and pick
   *thinksynth Juno* (CLAP or VST3).
3. Arm the track for recording and set its input to *MIDI → All MIDI
   inputs*, then play a MIDI keyboard; or open *View → Virtual MIDI
   keyboard*.
4. The plugin's window lists every parameter as a slider. Reaper draws
   these itself -- the plugin has no editor of its own yet.

## Parameters

One per control `juno.dsp` declares, in the order it declares them, with
its label and range, and then *Level*, the channel's amplitude (0..127, at
30). A time control is in milliseconds, as the file writes it, and is
converted at the host's sample rate; a saved project means the same thing
at any rate.

A host keys a parameter by its position in that list -- DPF gives CLAP and
VST3 no stable id of its own yet -- so reordering the controls in the
`.dsp` moves saved automation onto the wrong one. Adding a control at the
end does not.

Level 30 is where the desktop's patch selector puts a patch, and leaves
room for chords; a single note peaks near -28 dBFS. Turn it up for a lone
line.

## Timing

The engine renders windows of 64 frames. A MIDI note or a parameter change
takes effect at the start of the window its frame falls in: up to 1.3 ms
early at 48 kHz, never late.

## Tests

With `THINK_BUILD_PLUGIN=ON`, ctest adds:

| Test | Checks |
|---|---|
| `plugincheck` | a small CLAP host plays a phrase through the built plugin in uneven block sizes, with a note, a parameter change and a release, and requires it to match `thSynth` rendering the same graph bit for bit, at 48 and 44.1 kHz; and the parameter list |
| `plugin.clap-validator` | [clap-validator](https://github.com/free-audio/clap-validator) on the CLAP |
| `plugin.pluginval` | [pluginval](https://github.com/Tracktion/pluginval) on the VST3, at strictness 10 |

The validators are Linux builds, so those two are Linux only;
`plugincheck` needs `dlopen`, so it is not built on Windows.
