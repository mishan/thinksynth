# The headless harnesses.
#
# These need no audio backend and no plugins beyond the ones in the build
# tree, which is what makes them usable as CI gates on all three platforms.
# Nine are registered with add_test below; the rest take a corpus argument or
# their own build tree and are run by hand.
#
# editorcheck and composercheck are the exceptions to "no display": they build
# real widgets and skip themselves loudly without one, which is why CI runs the
# Linux suite under xvfb-run.

# think_harness(<name> [EXTRA <source>...])
function(think_harness name)
  cmake_parse_arguments(H "NODEMODEL;THREADS;VISUALHOST;COMPOSERHOST;NODECANVAS;COMPOSERCANVAS;ROLLCANVAS;PANEL;PATCH;DSPCATALOG;GENCATALOG" ""
                        "EXTRA" ${ARGN})

  set(sources "${name}.cpp" ${H_EXTRA})

  if(H_NODEMODEL)
    list(APPEND sources $<TARGET_OBJECTS:think_nodemodel>)
  endif()

  if(H_VISUALHOST)
    list(APPEND sources $<TARGET_OBJECTS:think_visualhost>)
  endif()

  if(H_COMPOSERHOST)
    list(APPEND sources $<TARGET_OBJECTS:think_composerhost>)
  endif()

  # What a parameter panel is (src/PanelModel.h) and the providers that build
  # one. No toolkit, which is the whole reason panelcheck can be a gate on
  # every platform rather than a thing editorcheck has to press a widget to
  # ask about.
  if(H_PANEL)
    list(APPEND sources $<TARGET_OBJECTS:think_panel>)
  endif()

  # What a .patch says (src/PatchFile.h). No synth and no filesystem, so
  # patchcheck can hold the format still on every platform without a corpus
  # directory to stand in.
  if(H_PATCH)
    list(APPEND sources $<TARGET_OBJECTS:think_patch>)
  endif()

  # What a .dsp says before anything loads it (src/DspCatalog.h). No graph
  # and no plugins, which is what lets dspcatalog check a chooser's list on
  # every platform -- and, where a corpus is at hand, against the parser.
  if(H_DSPCATALOG)
    list(APPEND sources $<TARGET_OBJECTS:think_dspcatalog>)
  endif()

  # And what a .gen says (src/GenCatalog.h), read through thcGenEdit, so a
  # harness that wants the piece list asks for this and the composer host.
  if(H_GENCATALOG)
    list(APPEND sources $<TARGET_OBJECTS:think_gencatalog>)
  endif()

  # A canvas's content (src/CanvasContent.h): the base and the one canvas
  # asked for. The widget shell around it is gtkmm and comes in through
  # EXTRA, since a harness that wants a widget links GTKMM itself.
  if(H_NODECANVAS OR H_COMPOSERCANVAS OR H_ROLLCANVAS)
    list(APPEND sources $<TARGET_OBJECTS:think_canvas>)
  endif()

  if(H_NODECANVAS)
    list(APPEND sources $<TARGET_OBJECTS:think_nodecanvas>)
  endif()

  if(H_COMPOSERCANVAS)
    list(APPEND sources $<TARGET_OBJECTS:think_composercanvas>)
  endif()

  if(H_ROLLCANVAS)
    list(APPEND sources $<TARGET_OBJECTS:think_rollcanvas>)
  endif()

  add_executable(${name} ${sources})

  target_include_directories(${name} PRIVATE
      "${PROJECT_SOURCE_DIR}"
      "${PROJECT_SOURCE_DIR}/src")

  target_link_libraries(${name} PRIVATE think)

  if(H_VISUALHOST)
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/libthink")
    target_link_libraries(${name} PRIVATE PkgConfig::CAIRO)
  endif()

  # The composer host runs off the Glib main loop in the app, but the
  # scheduler's virtual clock (stepTransport) is exactly what lets a harness
  # drive it with no loop at all -- glibmm links for g_get_monotonic_time and
  # sigc, not for a running loop.
  if(H_COMPOSERHOST)
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/libthink")
    target_link_libraries(${name} PRIVATE PkgConfig::GLIBMM)
  endif()

  if(H_NODECANVAS OR H_COMPOSERCANVAS OR H_ROLLCANVAS)
    target_link_libraries(${name} PRIVATE PkgConfig::CAIROMM)
  endif()

  if(H_THREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(${name} PRIVATE Threads::Threads)
  endif()

  set_target_properties(${name} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/scripts")

  # The harnesses live in their own directory and link libthink, so on Windows
  # each one needs its own copy of the DLL beside it for the same reason the
  # application does.
  think_copy_runtime_dlls(${name})
endfunction()

# gthPatchfile.cpp lives in the binary rather than in libthink, so dspcheck
# pulls it in directly to cover the .patch path. It needs sigc++ but not
# gtkmm. Every harness that names it also asks for PATCH: what a .patch says
# is src/PatchFile.cpp's now, and the shell is what is left over.
# Data-file lookup: the answers have to stay true after the working directory
# moves. See the header of pathcheck.cpp for the bug that was not caught.
think_harness(pathcheck)

# Which libthink headers stand on their own. The test is the compile; see the
# header of includecheck.cpp for why the list is short and why it grows.
think_harness(includecheck)

# The first-run configuration. Needs gthPrefs and gthPatchfile, which live in
# the binary rather than in libthink, and glibmm for the config directory --
# but not gtkmm: none of this draws anything.
think_harness(configcheck PATCH
              EXTRA "${PROJECT_SOURCE_DIR}/src/gthPatchfile.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gthPrefs.cpp")
target_link_libraries(configcheck PRIVATE PkgConfig::GLIBMM PkgConfig::SIGC)

# The light/dark decision. Only the Linux half of the platform readers can be
# run here at all, so what gets pinned is the part that is the same on all
# three -- and that part lives in its own file precisely so this harness needs
# neither gtkmm nor, on macOS, CoreFoundation. Compiling all of gthTheme.cpp
# to reach it is what broke the macOS build.
think_harness(themecheck EXTRA "${PROJECT_SOURCE_DIR}/src/gthThemePolicy.cpp")

think_harness(dspcheck PATCH EXTRA "${PROJECT_SOURCE_DIR}/src/gthPatchfile.cpp")

think_harness(dsplevel)
think_harness(dspab)

# Arithmetic over signals: that a constant expression still folds, that one
# with a signal in it renders as the nodes it stands for, and that the six
# function nodes compute what they say. Its own harness because none of those
# cases is in the corpus and the first two are equivalences rather than
# loads -- dspcheck can say a file parsed and not that two files agree.
think_harness(exprcheck NODEMODEL)

# The graph that runs on a channel's summed voices: that it runs when nothing
# is playing, that in0 carries the audio, that its chanargs are its own under
# `fx.', and that it comes and goes with the channel. Loads the shipped effect
# graphs too, since the note-playing corpus gates skip them.
think_harness(fxcheck)

# How a channel hands notes to voices: `poly' and `mono' on the io node, the
# held-key stack behind mono's last-note priority, and the glide a misc::slew
# carries across a retune. Listens to the channel's output rather than reaching
# into thMidiChan, and writes its own .dsp files -- every shipped graph is
# polyphonic, so the corpus cannot hold these cases.
think_harness(voicecheck)

# The three stateful nodes -- misc::slew, filt::svf, osc::noise -- measured:
# a step response, a frequency response, a spectral tilt, and for each of them
# that a window boundary is not an event. Builds its graphs in C++ rather than
# parsing them, because the inputs it needs are not things the corpus holds.
think_harness(statecheck)

# The non-finite gate: every graph at the corners of every control it
# declares, plus the engine guard's own claim about what one bad voice costs.
# See the header of dspsweep.cpp for why the corners are swept one control at
# a time, and scripts/guard/ for the two fixtures the guard's gate uses.
think_harness(dspsweep)
# dspstress runs each level in a forked child with an alarm(2) watchdog, so
# that a level which segfaults or wedges does not take the run with it. That
# is fork(), waitpid() and SIGALRM -- none of which Windows has, and the
# obvious rewrite (threads, no isolation) would lose the property that makes
# the harness useful. It is also a ThreadSanitizer harness, and there is no
# TSan for MinGW. So it is simply not built there.
if(NOT WIN32)
  think_harness(dspstress THREADS)
endif()

# The audio path's block-size handling, without a sound card. gthSynthSource
# is deliberately free of both gtkmm and RtAudio so this can link it directly.
think_harness(dspblock EXTRA "${PROJECT_SOURCE_DIR}/src/gthSynthSource.cpp")

# And the same path backwards: a live input accumulated from device periods
# into windows. Its own harness rather than three more block sizes in dspblock,
# because the property is not the same property -- see the head of
# dspcapture.cpp.
think_harness(dspcapture EXTRA "${PROJECT_SOURCE_DIR}/src/gthSynthSource.cpp")

# The MIDI thread handoff, without a MIDI device. gthMidiQueue needs glibmm
# for Glib::Dispatcher but not gtkmm, so this stays headless.
think_harness(dspmidi THREADS EXTRA "${PROJECT_SOURCE_DIR}/src/gthMidiQueue.cpp")
target_link_libraries(dspmidi PRIVATE PkgConfig::GLIBMM)

# thSampleRing on its own, with no synth around it. Header-only, so this links
# libthink only for the include path.
think_harness(ringcheck THREADS)

# The visual plugins, drawn to a cairo image surface. No toolkit and no
# display, which is the practical reason the visual ABI takes a cairo_t rather
# than a widget -- it means these gate on all three CI runners.
think_harness(visualcheck VISUALHOST)
# plugins/visual for fftr.h: visualcheck checks the shared FFT numerically,
# which is the one place in the visualizer work where "are the numbers right"
# has an answer.
target_include_directories(visualcheck PRIVATE "${PROJECT_SOURCE_DIR}/plugins/visual")

# dspprobe uses NodeGraph to enumerate output ports, so that what it checks is
# what the editor would offer rather than a second opinion about which args are
# outputs.
think_harness(dspprobe  NODEMODEL)

# The one harness here that needs a display, because what it measures is a real
# widget's real draw path. A measuring instrument rather than a gate, so it is
# not in the ctest list below -- run it by hand, under xvfb-run if need be.
think_harness(canvasbench NODEMODEL VISUALHOST NODECANVAS
              EXTRA "${PROJECT_SOURCE_DIR}/src/gui/GraphCanvas.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/NodeCanvasWidget.cpp")
target_link_libraries(canvasbench PRIVATE PkgConfig::GTKMM)

# editorcheck joins up what the other harnesses cover separately: the tap, the
# module and the panel are three things, and NodeEditor is the only thing that
# holds them together. It needs a display for the same reason canvasbench does
# -- real widgets -- but unlike canvasbench it is a gate, so it is in the ctest
# list below, guarded by whether a display can be had.
# The panel and gthPatchfile join the list for the channel-effect panel: a
# channel has two chanarg maps and one lookup reaches both, so the panel over
# the second differs from the panel over the first by a prefix -- and the way
# that fails is a slider that moves and changes nothing.
think_harness(editorcheck NODEMODEL VISUALHOST NODECANVAS PANEL PATCH
              EXTRA "${PROJECT_SOURCE_DIR}/src/gui/GraphCanvas.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/NodeCanvasWidget.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/NodeEditor.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/NodeParams.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/NodePalette.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/PanelView.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/ArgPanelView.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/ColumnUtil.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gthPatchfile.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/Dialogs.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/SaveButton.cpp")
target_link_libraries(editorcheck PRIVATE PkgConfig::GTKMM)

# The composer window, driven the way editorcheck drives the node editor.
# Its own harness rather than more EXTRA on that one: the two share no code
# and a link chain that pulls the whole GUI into one binary makes a failure
# harder to attribute, which is the reason the levels in dspstress exist too.
#
# gthPatchfile joins the list because a piece carries its own instruments now,
# and the window brings one up the way the Patch Selector does. Linking it is
# what makes that path real here rather than stubbed: the harness gets an
# actual patch manager and the instrument actually lands on a tab.
think_harness(composercheck COMPOSERHOST VISUALHOST COMPOSERCANVAS ROLLCANVAS
              PATCH GENCATALOG
              EXTRA "${PROJECT_SOURCE_DIR}/src/gui/GraphCanvas.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/ComposerWindow.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/ItemBrowser.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/ComposerCanvasWidget.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/PianoRoll.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/Dialogs.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gthPatchfile.cpp")
target_link_libraries(composercheck PRIVATE PkgConfig::GTKMM)

# The piano roll's drawing, with no window around it: that the future half is
# drawn at all, that a gesture moves the view, and -- through `--dump' and
# wasm/web/rollcheck.mjs -- that the browser's copy of the same class draws
# the same list at the same frame.
#
# The one binary in this tree that does not link cairo. It links
# cairo-canvas2d instead, which is the recorder the browser module draws
# through, so what comes out of a draw here is the list of ops the other side
# produces rather than pixels nothing can compare. RollCanvas.cpp and
# CanvasContent.cpp are compiled again for it, with that directory ahead of
# everything on the include path so <cairo.h> and <cairomm/context.h> resolve
# to the stand-in; the object libraries next door are the same sources against
# real cairomm, for the application.
#
# cairo-canvas2d comes from npm and lives under wasm/web/node_modules, which
# the browser build takes it out of by path. So this is built where it is
# there and skipped where it is not, with a line saying so: a native tree is
# not required to have run `npm ci' to build the synth, and the gate that
# needs this binary (.github/workflows/ci.yml, wasm-pieces) runs npm ci before
# the native build for exactly this reason.
set(CAIRO2D_DIR "${PROJECT_SOURCE_DIR}/wasm/web/node_modules/cairo-canvas2d")

if(NOT EXISTS "${CAIRO2D_DIR}/cairo2d.cpp")
  message(STATUS
    "scripts: no cairo-canvas2d, so no rollcheck. Run `npm ci' in "
    "wasm/web and configure again; nothing else here needs it.")
else()
  think_harness(rollcheck COMPOSERHOST
                EXTRA "${PROJECT_SOURCE_DIR}/src/CanvasContent.cpp"
                      "${PROJECT_SOURCE_DIR}/src/RollCanvas.cpp"
                      "${CAIRO2D_DIR}/cairo2d.cpp")

  # BEFORE, and no PkgConfig::CAIROMM at all: the stand-in's cairo.h and
  # cairomm/context.h are the ones these three files are to see, and the
  # surest way to be sure of that is for the real ones not to be on the path.
  target_include_directories(rollcheck BEFORE PRIVATE "${CAIRO2D_DIR}")

  # gen/belfry.gen because the future half needs a piece that has one: its
  # composers queue a phrase ahead rather than a note at a time, so there
  # are scheduled notes on screen to the right of the now-line at any
  # instant. Plenty of shipped pieces emit note by note and would pass
  # every other check here while saying nothing about the half this roll
  # was built for. Seeded, so the browser can be held against it
  # (wasm/web/rollcheck.mjs).
  #
  # THINK_DSP_PATH for gencheck's reason: the piece carries its own
  # instruments and an instrument that cannot be found is a piece that
  # does not load.
  add_test(NAME rollcheck
           COMMAND rollcheck -p "${PROJECT_BINARY_DIR}/plugins/"
                   "${PROJECT_SOURCE_DIR}/gen/belfry.gen")

  set_tests_properties(rollcheck PROPERTIES
      ENVIRONMENT "THINK_DSP_PATH=${PROJECT_SOURCE_DIR}/dsp")
endif()

# What a parameter *is*: the plugin's declaration, the pass that carries it to
# the control driving it, and the graph that draws the result. Builds its own
# .dsp files -- the cases that matter are ones the corpus cannot contain.
think_harness(argtype   NODEMODEL)

# What a parameter panel says, before anybody draws it: units unfolded at the
# synth's rate, decimals that suit the range, a selector's holes, the groups,
# and what a typed value is allowed to become. Those were rules inside a
# widget-building function, which meant they could only be checked on a
# machine with a display and were re-implemented, badly, for the browser.
# Needs the plugins and nothing else: it writes its own .dsp files.
think_harness(panelcheck PANEL)

# The .patch format (src/PatchFile.h): every shipped patch parses and says
# nothing the reader has to complain about, parse -> compose -> parse is
# stable, and a fixture per awkward line asserts the documented answer. Needs
# no plugins and no channel -- a document is a string, which is the whole
# point of having one.
think_harness(patchcheck PATCH)

# What a chooser would show: every shipped .dsp's title, description and kind,
# read out of the header rather than out of a built graph -- and each one put
# beside what the parser says about the same file, since a format read twice
# is a format free to drift. Fixtures for the headers the corpus cannot have.
think_harness(dspcatalog DSPCATALOG)

# The composer framework: the .gen loader's validation (each bad file is
# built on the fly and must fail with the right complaint), the shared pitch
# parser, and the replay-determinism contract -- same file, same seed, same
# delivered event stream, byte for byte. Headless: the scheduler is driven
# through its virtual clock, no Glib loop and no audio.
think_harness(gencheck COMPOSERHOST GENCATALOG)

# A piece, rendered: the same virtual clock gencheck drives, with the synth
# rendering a window after every step and the windows written out as a WAV,
# the delivered events as a tape. For hearing a .gen without the window and
# for reading a level off a new instrument block. Not a test.
think_harness(genwav COMPOSERHOST)

# The lexical layer .dsp and .gen now share: kinds, lines, byte spans, what it
# refuses, and that .gen's tokenize() is still an adapter over that scan
# rather than a second scanner growing back. Needs the composer host only to
# reach thcGenLoader::tokenize.
think_harness(lexcheck COMPOSERHOST THREADS)

# The two hosts, diffed. Same graph, same rate, same plugin binaries, cut into
# windows of one and into a window of two hundred -- which is the difference
# between what the composer's control-rate host does and what the audio thread
# does, and the hazard docs/UNIFICATION.md named before either host existed.
# Needs no corpus and no display: it builds its graphs in C++.
think_harness(hostcheck)

# docs/NODES.md, out of the plugins themselves. Uses the palette's catalogue walk,
# so the reference covers exactly what the editor offers.
think_harness(dspnodes  NODEMODEL)

# `cmake --build <build> --target kit' fills dsp/samples/ with the wavs
# osc::sample plays, by rendering the tree's own drum graphs through
# genwav. Not a test and not part of `all': it writes into the *source*
# tree, which is a thing a build should only ever do when asked.
#
# The wavs are committed, because a clone that has to run a target before
# a piece will sound is a clone that does not work. This target is for
# regenerating them after a drum graph changes.
#
# UNIX only: it is a shell script, and the alternative -- a second copy
# of the same list in CMake -- is a list that goes stale. Nothing depends
# on it, so a Windows build is missing a convenience and not a step.
if(UNIX)
  add_custom_target(kit
      COMMAND "${CMAKE_COMMAND}" -E env
              "GENWAV=$<TARGET_FILE:genwav>"
              "PLUGINS=${PROJECT_BINARY_DIR}/plugins"
              "DSP=${PROJECT_SOURCE_DIR}/dsp"
              "OUT=${PROJECT_SOURCE_DIR}/dsp/samples"
              "${PROJECT_SOURCE_DIR}/scripts/makekit.sh"
      DEPENDS genwav plugins
      COMMENT "Rendering dsp/samples/ from the tree's own drum graphs")
endif()

# `cmake --build <build> --target nodes' after touching a plugin's arg
# declarations. The ctest below fails if anybody forgets.
add_custom_target(nodes
    COMMAND dspnodes -p "${PROJECT_BINARY_DIR}/plugins/"
            -o "${PROJECT_SOURCE_DIR}/docs/NODES.md"
    DEPENDS dspnodes plugins
    COMMENT "Regenerating docs/NODES.md from the plugins")

think_harness(dspgraph  NODEMODEL)
think_harness(dsplayout NODEMODEL)
think_harness(dsplive   NODEMODEL)
think_harness(dspnew    NODEMODEL)
think_harness(dspwrite  NODEMODEL)

# One edit, printed: the reference the browser's node editor is compared
# against (wasm/web/nodecheck.mjs). Not a gate of its own -- it makes no
# claims -- which is why it is not in the ctest list below.
think_harness(dspedit   NODEMODEL)

# ---------------------------------------------------------------------------
# CTest gates. See cmake/RunHarness.cmake for why the file lists are computed
# at test time rather than at configure time.
# ---------------------------------------------------------------------------

set(_runner "${PROJECT_SOURCE_DIR}/cmake/RunHarness.cmake")

add_test(NAME dspcheck.dsp
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspcheck>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/dsp
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=dsp
                 "-DEXTRA_ARGS=-q --shipped"
                 -P "${_runner}")

# The fixtures beside this harness, which are not in dsp/ and so are in no
# corpus sweep. They still have to load: divergent.dsp is the only user of
# filt::divbuf in the tree, and a fixture nothing ever loads is a fixture
# that has quietly stopped being one. Loading is all that is claimed here --
# what these files do once loaded is dspsweep.guard's business.
add_test(NAME dspcheck.guard
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspcheck>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/scripts/guard
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/scripts/guard
                 -DMODE=dsp
                 -DEXTRA_ARGS=-q
                 -P "${_runner}")

add_test(NAME dspcheck.patch
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspcheck>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/patches
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DWORKING_DIR=${PROJECT_SOURCE_DIR}/dsp
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=patch
                 -DEXTRA_ARGS=-q
                 -P "${_runner}")

# Exit status is the number of measurements still exceeding TH_MAX, which
# should be zero at every voice count. The graph and the layout block,
# over the whole corpus -- and twice over for the layout: every read and
# write is made once over a file and once over the same bytes as text,
# which is what a browser tab has instead of a file.
add_test(NAME dspgraph
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspgraph>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/dsp
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=dsp
                 -P "${_runner}")

# The writer, over the whole corpus -- and twice over: every edit is made
# once over a file and once over the same bytes as text, and the two have to
# be the same edit. It was never a ctest gate because it takes a corpus
# argument; RunHarness.cmake is what that objection was about, and dspcheck
# has used it all along.
add_test(NAME dspwrite
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspwrite>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/dsp
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=dsp
                 -P "${_runner}")

# The non-finite gate over the whole corpus: every control at its .min, its
# .max and three points between, a note at each end of the keyboard, and the
# engine's guard asked afterwards whether it caught anything. Thousands of
# renders, so slower than the other corpus gates.
add_test(NAME dspsweep
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspsweep>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/dsp
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=dsp
                 -DEXTRA_ARGS=-q
                 -P "${_runner}")

# The committed reference against a fresh one. A plugin whose arg
# declarations changed without docs/NODES.md being regenerated fails here, which is
# what keeps the two from drifting.
add_test(NAME dspnodes
         COMMAND dspnodes -p "${PROJECT_BINARY_DIR}/plugins/"
                 -c "${PROJECT_SOURCE_DIR}/docs/NODES.md")

# The guard's own claim, which no corpus file can make: one instrument that
# cannot help going non-finite and one that behaves, on two channels at once.
# Its own test rather than more EXTRA_ARGS on the sweep above, because a path
# with a space in it does not survive EXTRA_ARGS and this one is a path.
add_test(NAME dspsweep.guard
         COMMAND dspsweep -p "${PROJECT_BINARY_DIR}/plugins/"
                 -g "${PROJECT_SOURCE_DIR}/scripts/guard")

# And the same piece through genwav, for the status and the summary line a
# script asks about. See cmake/RunGuardPiece.cmake for the three claims.
add_test(NAME genwav.nonfinite
         COMMAND "${CMAKE_COMMAND}"
                 -DGENWAV=$<TARGET_FILE:genwav>
                 -DPIECE=${PROJECT_SOURCE_DIR}/scripts/guard/nonfinite.gen
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/scripts/guard
                 -P "${PROJECT_SOURCE_DIR}/cmake/RunGuardPiece.cmake")

add_test(NAME genwav.meters
         COMMAND "${CMAKE_COMMAND}"
                 -DGENWAV=$<TARGET_FILE:genwav>
                 -DPIECE=${PROJECT_SOURCE_DIR}/scripts/guard/meters.gen
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DTAPE=${PROJECT_BINARY_DIR}/genwav-meters.tape
                 -P "${PROJECT_SOURCE_DIR}/cmake/RunGenwavMeters.cmake")

add_test(NAME dsplevel
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dsplevel>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/dsp
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=dsp
                 "-DEXTRA_ARGS=-v 4"
                 -P "${_runner}")

# Rendering each patch eight times over is slow, so this runs on a
# representative handful rather than the whole corpus: what is being tested is
# gthSynthSource, not the DSPs.
add_test(NAME dspmidi COMMAND dspmidi 200000)

add_test(NAME dspblock
         COMMAND dspblock -q -p "${PROJECT_BINARY_DIR}/plugins/"
                 "${PROJECT_SOURCE_DIR}/dsp/ts1.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/anasync.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/mfm01.dsp")

# Writes its own graphs, so it needs the plugins and nothing else.
add_test(NAME dspcapture
         COMMAND dspcapture -p "${PROJECT_BINARY_DIR}/plugins/")

# Cheap and entirely self-contained, so it runs the full sweep rather than a
# representative sample.
add_test(NAME ringcheck COMMAND ringcheck 20000)

add_test(NAME pathcheck COMMAND pathcheck)

# Needs the plugins, because "the channel loaded" means its DSP parsed. Makes
# its own config directory and points HOME at it, so it neither reads nor
# writes the preferences of whoever runs it.
#
# THINK_PATCH_PATH and THINK_DSP_PATH for the reason RunHarness.cmake gives at
# length: without them the only thing left pointing at a corpus is the
# compiled-in PATCH_PATH, and on a development machine that is answered by
# whatever was last installed to /usr/local. This test was registered without
# them and passed here and failed on every clean runner, which is the same way
# round the patch gate got it wrong before.
add_test(NAME configcheck
         COMMAND configcheck -p "${PROJECT_BINARY_DIR}/plugins/")

set_tests_properties(configcheck PROPERTIES
    ENVIRONMENT "THINK_PATCH_PATH=${PROJECT_SOURCE_DIR}/patches;THINK_DSP_PATH=${PROJECT_SOURCE_DIR}/dsp")

add_test(NAME themecheck COMMAND themecheck)

# Registered so the log names what was covered; the assertion already fired at
# compile time.
add_test(NAME includecheck COMMAND includecheck)

# Needs no corpus and no display -- it writes its own .dsp files.
add_test(NAME argtype
         COMMAND argtype -p "${PROJECT_BINARY_DIR}/plugins/")

add_test(NAME exprcheck
         COMMAND exprcheck -p "${PROJECT_BINARY_DIR}/plugins/")

# Needs the plugins and nothing else: its graphs are built in C++.
add_test(NAME statecheck
         COMMAND statecheck -p "${PROJECT_BINARY_DIR}/plugins/")

# Needs the plugins and nothing else: it writes its own .dsp files.
add_test(NAME voicecheck
         COMMAND voicecheck -p "${PROJECT_BINARY_DIR}/plugins/")

# The effect path, plus every shipped effect graph as an argument -- they are
# the ones the note-playing gates skip, so this is where they are covered.
add_test(NAME fxcheck
         COMMAND fxcheck -p "${PROJECT_BINARY_DIR}/plugins/"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/echo.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/hall.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/chorus.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/limiter.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/flanger.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/gate.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/phaser.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/rotary.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/vocoder.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/fx/comp.dsp")

# Needs no corpus and no display -- it draws to an image surface.
add_test(NAME visualcheck
         COMMAND visualcheck -p "${PROJECT_BINARY_DIR}/plugins/")

# The .gen loader and the replay gate. Needs the composer plugins from the
# build tree and the shipped piece; no display, no audio, no main loop.
#
# THINK_DSP_PATH because a piece now carries its own instruments and the
# instrument check loads real graphs. Without it the only thing left pointing
# at a corpus is the compiled-in DSP_PATH, which on a development machine is
# answered by whatever was last installed to /usr/local -- the same way round
# the configcheck registration got it wrong.
add_test(NAME gencheck
         COMMAND gencheck -p "${PROJECT_BINARY_DIR}/plugins/"
                 "${PROJECT_SOURCE_DIR}/gen/airports.gen")

set_tests_properties(gencheck PROPERTIES
    ENVIRONMENT "THINK_DSP_PATH=${PROJECT_SOURCE_DIR}/dsp")

# Needs the plugins and nothing else: it writes its own .dsp files, and the
# cases that matter -- a selector with a hole in it, a group of one -- are
# ones the shipped corpus is careful not to have.
add_test(NAME panelcheck
         COMMAND panelcheck -p "${PROJECT_BINARY_DIR}/plugins/")

# The corpus, because "no shipped file changed meaning" is the claim this
# extraction rests on; the fixtures run either way.
add_test(NAME patchcheck
         COMMAND patchcheck "${PROJECT_SOURCE_DIR}/patches")

# Needs no corpus, no plugins and no display -- it writes its own source text.
add_test(NAME lexcheck COMMAND lexcheck)

# The corpus, because "a row says what loading the file would have said" is a
# claim about the shipped graphs; the fixtures run either way. The plugins are
# for the half that loads.
add_test(NAME dspcatalog
         COMMAND dspcatalog -p "${PROJECT_BINARY_DIR}/plugins/"
                 "${PROJECT_SOURCE_DIR}/dsp")

# Needs the plugins, because what it compares is two runs of the real ones.
add_test(NAME hostcheck
         COMMAND hostcheck -p "${PROJECT_BINARY_DIR}/plugins/")

add_test(NAME dspprobe
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspprobe>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/dsp
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=dsp
                 -DEXTRA_ARGS=-q
                 -P "${_runner}")

# Needs a display for the same reason editorcheck does, and skips itself the
# same way.
add_test(NAME composercheck
         COMMAND composercheck -p "${PROJECT_BINARY_DIR}/plugins/"
                 "${PROJECT_SOURCE_DIR}/gen/airports.gen")

# Same reason as gencheck: the window opens a piece that carries its own
# instrument, and an instrument that cannot be found is a piece that does not
# load.
set_tests_properties(composercheck PROPERTIES
    ENVIRONMENT "THINK_DSP_PATH=${PROJECT_SOURCE_DIR}/dsp")

# Skips itself, loudly, where there is no display -- see the comment in
# editorcheck.cpp. CI runs the whole suite under xvfb-run so that it does not.
add_test(NAME editorcheck
         COMMAND editorcheck -p "${PROJECT_BINARY_DIR}/plugins/"
                 "${PROJECT_SOURCE_DIR}/dsp/ts1.dsp")

# THINK_DSP_PATH for the channel-effect panel: it loads dsp/fx/echo.dsp, and
# without this the only thing left pointing at a corpus is the compiled-in
# DSP_PATH, which on a development machine is answered by whatever was last
# installed to /usr/local -- the same way round the configcheck registration
# got it wrong.
set_tests_properties(editorcheck PROPERTIES
    ENVIRONMENT "THINK_DSP_PATH=${PROJECT_SOURCE_DIR}/dsp")

set_tests_properties(dspcheck.dsp dspcheck.guard dspcheck.patch dsplevel dspblock dspmidi
                     ringcheck visualcheck dspprobe editorcheck argtype
                     exprcheck statecheck voicecheck fxcheck gencheck
                     panelcheck
                     dspsweep dspsweep.guard
                     genwav.nonfinite
                     PROPERTIES TIMEOUT 900)

# The two harnesses that build real widgets need LeakSanitizer told which
# leaks are the toolkit's. See scripts/lsan.supp for what is in it and what
# is deliberately left out; the short version is that a process which draws
# one frame and exits leaves fontconfig's caches and a software GL driver
# behind, and none of it is ours.
#
# Set here rather than in the CI workflow so that `-DTHINK_SANITIZE=address'
# behaves the same on a developer's machine as it does on a runner -- a gate
# that only passes in CI teaches people to stop running it locally. Only
# under the address sanitizer, so an ordinary run carries none of it.
#
# APPEND, not a second set_tests_properties. ENVIRONMENT is one list property,
# and setting it replaces whatever was there -- so this block used to delete
# the THINK_DSP_PATH composercheck was given above, and only under the
# sanitizer. The piece it opens carries its own instrument now, so the missing
# path meant the .dsp did not resolve, the piece did not load, and seven checks
# failed on the runner while every one of them passed locally. A gate that
# disagrees with CI about what it is testing is worse than no gate.
if(THINK_SANITIZE MATCHES "address")
  foreach(_t composercheck editorcheck)
    set_property(TEST ${_t} APPEND PROPERTY ENVIRONMENT
        "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/scripts/lsan.supp")
  endforeach()
endif()
