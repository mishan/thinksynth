# The plugin set.
#
# Lists are explicit, one name per plugin, and deliberately not file(GLOB).
# What is *missing* here is load-bearing:
#
#   - input/ (wav, alsa) and test/ are not in configure.ac's PLUGIN_DIRS.
#     wav.cpp's own description string reads "Wav Input (BROKEN)", and
#     test.cpp still calls SetState/SetDesc, renamed years ago.
#   - fft/ has no sources at all.
#   - misc/wlan is in the directory but not in misc/Makefile's PLUGIN_NAMES;
#     it needs libiw.
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
