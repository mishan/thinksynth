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

Outside the C and C++ runtimes, the plugin links the system's
`libsigc++-3.0`, and for its editor X11 (with Xcursor, Xext and Xrandr),
D-Bus and cairo -- and what those pull in: fontconfig, freetype, libpng
and xcb. The engine is only in the DSP side: an LV2 bundle's editor
module, a file of its own, carries none of it.

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
4. The FX window shows the plugin's own editor (below). Reaper's *UI*
   button switches to its generic list of sliders; in the VST3 that list
   starts with 2,080 *MIDI Ch. n CC m* entries -- VST3 has no MIDI
   controllers, so a plugin that wants them declares a hidden parameter
   for each, and Reaper lists hidden ones. The CLAP has none of them.

## The editor

A panel of knobs, one per parameter, in titled boxes -- one per
`@x.group` the `.dsp` declares, in the order it first names each, and
*Output* for the level. `juno.dsp` groups its controls as *Oscillator*,
*Filter*, *Chorus* and *Envelope*.

| Gesture | Does |
|---|---|
| drag up / down | turns the knob; the full travel is 200 pixels |
| Shift-drag | ten times finer |
| mouse wheel | a fiftieth of the travel a notch (Shift: a five-hundredth); a whole-numbered control moves by one |
| double-click | back to the `.dsp`'s value |

A drag is one gesture to the host, so automation records it as one. A range
that starts above zero and spans more than a factor of twenty -- *Cutoff*,
the rates, *Filter Decay* -- turns on a log scale; the host still sees the
plain value. A value is shown in the `.dsp`'s units, and a label ending in
one in brackets, `Cutoff (Hz)`, is drawn *Cutoff* over *700 Hz*.

The drawing and the knob arithmetic are `plugin/Panel.cpp`, which knows
nothing about windows; `plugin/ThinkUI.cpp` is the DPF window around it,
scaled by the desktop's scale factor or the one a host sets. The control
list both sides use is read off the engine at build time by
`plugin/controlsgen.cpp`, which also measures the panel, so the size a
host is told before the editor opens is the editor's. A knob the editor
turns on a log scale is hinted logarithmic to the host too, so its own
controls agree. `uicheck` renders the panel to `build/plugin/editor.png`.

One thing DPF does not pass on: the pointer leaving the window. A knob
the pointer was over when it left stays lit until the pointer comes
back.

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

## Saving

A host saves the plugin's state in its project, and DPF writes it as each
parameter's symbol -- the `.dsp`'s name for the control, `cutoff` -- and
its value, in the C locale. So a saved project survives the controls being
reordered, which automation does not; a control that is gone is skipped
and a new one keeps its default.

## Timing

The engine renders windows of 64 frames and applies an event only at a
window's start. The plugin applies each event at the start of the window
its frame falls in, which it can do only once the host has sent that whole
window, so it renders each window when the host reaches its end: one window
of latency, 64 frames (1.3 ms at 48 kHz), which it reports and a host
compensates. The output is exactly the engine's, one window later.

A MIDI event carries its frame. A parameter change does not -- DPF sets it
before the block with no frame -- so it takes effect at the window its
block starts in.

## MIDI

Every MIDI channel plays the one instrument, and a key held on two
channels is released when the last of them lets go. Controllers go to the
graph (the sustain pedal, CC 64, among them). All notes off (CC 123)
releases whatever is held and all sound off (CC 120) cuts it; and what was
sounding when the host stopped processing is gone when it starts again.

## Tests

With `THINK_BUILD_PLUGIN=ON`, ctest adds:

| Test | Checks |
|---|---|
| `plugincheck` | the compiled-in control table against the engine's own parse; a small CLAP host plays a phrase through the built plugin in uneven block sizes -- notes on two channels, a parameter change, all notes off and all sound off, at frames inside windows and blocks -- and requires it to be `thSynth` rendering the same graph one window later, bit for bit, at 48 and 44.1 kHz; the reported latency; a chord gone after the host stops and starts; the parameter list; and a project's round trip, state saved from one instance and loaded into a fresh one |
| `plugin.clap-validator` | [clap-validator](https://github.com/free-audio/clap-validator) on the CLAP |
| `plugin.pluginval` | [pluginval](https://github.com/Tracktion/pluginval) on the VST3, at strictness 10, opening and automating the editor |
| `uicheck` | the editor with no window: every knob found where it is drawn, drag and wheel arithmetic, the log scale, NaN and out-of-range values, a panel of ungrouped knobs wrapping, long group names clipped, how values are spelled, and the panel drawn at 1x and 2x |
| `guicheck` | the editor through CLAP as a real window, at a scale of 1 and of 2: its size, its pixels read back off the X server against the panel's own drawing, and a knob dragged with XTest input landing where the panel's arithmetic says, as one gesture to the host |

The validators are Linux builds, and `guicheck` needs X11, so those three
are Linux only; `plugincheck` needs `dlopen`, so it is not built on
Windows. `guicheck` skips itself with no display; under ctest,
`scripts/headless.sh` gives it one.
