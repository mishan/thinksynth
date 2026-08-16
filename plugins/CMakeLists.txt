# The plugin set.
#
# Lists are explicit, one name per plugin, and deliberately not file(GLOB).
# What is *missing* here is load-bearing:
#
#   - input/ (wav, alsa) and test/ were excluded by the old build too, and
#     stay excluded here. wav.cpp's own description string reads "Wav Input
#     (BROKEN)", and test.cpp still calls SetState/SetDesc, renamed years ago.
#   - fft/ holds one .c file that is not a plugin: no module_init, no
#     module_callback, just fir_filter/iir_filter/gaussian/fft lifted from
#     Embree & Kimble's C Language Algorithms for DSP. Nothing in the tree
#     references it. VISUALIZERS.md says why the visualizers did not
#     reuse its FFT: it keeps its twiddle table in a function-static keyed on
#     the last size asked for, and it calls exit(1) when a calloc fails --
#     which is a library killing the host process, the exact pattern
#     that was removed from the parser years ago.
#   - misc/wlan is in the directory but has never been built: it needs libiw.
#
# Eleven of the 92 shipped DSPs reference those plugins and are expected to
# fail to parse. A glob would silently start building them and change what
# the corpus does.

include(ThinkPlugin)

add_custom_target(plugins)

think_add_plugin_category(analysis pitch)

think_add_plugin_category(delay    echo fir)

think_add_plugin_category(dist     clip saturate waveman inksat)

think_add_plugin_category(env      adsr adsfr ad dynmap follower followavg map)

think_add_plugin_category(filt     allpass comb resonator ink ink2 ink3
                                   inkshape rds ds dshape divbuf res1pole
                                   res2pole res2pole2 resgrav moog)

think_add_plugin_category(impulse  sine parabola square blackman)

think_add_plugin_category(logic    and not)

think_add_plugin_category(math     add sub mul div sin)

think_add_plugin_category(misc     latch noisegate midi2freq midi2range
                                   freq2samples print decibel)

think_add_plugin_category(mixer    add fade mul)

think_add_plugin_category(osc      static simple sinsaw buzzer softsqr softsqr2
                                   bandosc shapeo multiwave multisined window)

# Visualizers. Not DSP plugins: a different ABI, loaded by the GUI rather than
# by the parser, drawing with cairo and linking neither libthink nor sigc++.
# See src/thVisual.h and think_add_visual in cmake/ThinkPlugin.cmake.
#
# A .dsp cannot name one of these, so nothing in the corpus depends on which
# are built and the "what is missing is load-bearing" warning above does not
# apply to this list.
# Composers. A third ABI again: loaded by the GUI's scheduler, never by the
# parser, emitting note and chanarg events rather than samples. They link
# neither libthink nor cairo -- see think_add_composer in ThinkPlugin.cmake
# and libthink/thcomposer.h. As with the visuals, nothing in the DSP corpus
# can name one, so this list carries no compatibility weight.
think_add_composer(eno_line)
think_add_composer(walk)
think_add_composer(quantize)
think_add_composer(humanize)
think_add_composer(euclid)
think_add_composer(lsystem)
think_add_composer(evolve)
think_add_composer(markov)
think_add_composer(ca)
think_add_composer(arp)

# The exception that proves the no-cairo rule: a module with a
# composer_draw (euclid's onset ring, lsystem's contour, evolve's
# fitness curve, markov's transition heat, ca's grid) calls cairo
# functions and links cairo. The others still don't.
target_link_libraries(composer_euclid PRIVATE PkgConfig::CAIRO)
target_link_libraries(composer_lsystem PRIVATE PkgConfig::CAIRO)
target_link_libraries(composer_evolve PRIVATE PkgConfig::CAIRO)
target_link_libraries(composer_markov PRIVATE PkgConfig::CAIRO)
target_link_libraries(composer_ca PRIVATE PkgConfig::CAIRO)

think_add_visual(meter)
think_add_visual(scope)
think_add_visual(spectrum)
think_add_visual(spectrogram)
