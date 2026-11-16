# Bamboo Flute -- a transverse flute cut from one length of bamboo.
#
# A relative of the ryuteki and the shinobue, with the shakuhachi's way
# of bending a note. A bamboo bore is rough inside and its holes are
# open, so the tone is mostly fundamental: a recording of a shakuhachi
# at A4 has the second harmonic 15 dB down, the third 20 and the fourth
# 34. That is a sine with a band-limited saw under it, lowpassed near
# the third harmonic, and the breath is the rest.
#
# THE BREATH. Air across the embouchure hole is noise, and the bore
# colors it: one band of it rings at the note, which is the hollow
# under the tone, and the hiss above 3 kHz is the edge of the hole.
# There is more of both at the start of a note, where the jet has not
# yet locked to the bore. `Breath' is how much.
#
# BLOWING HARDER. A flute played louder is played brighter: a faster
# jet drives the upper harmonics, so velocity raises both the saw and
# the lowpass over it, and at full velocity the tone has the edge of a
# note about to overblow.
#
# THE SCOOP AND THE MERI. A player rarely lands on a note: it is
# slid into from below, `Scoop' semitones over `Scoop Time'. `Meri' is
# the shakuhachi's bend down, made by lowering the chin over the
# mouthpiece, and as there it darkens the tone and takes a little off
# its level. It is a knob, so it bends a note that is already held.
#
# THE VIBRATO. It arrives late, after `Vibrato Delay', and is breath
# as much as pitch: the level follows the bend, a few percent up and
# down with it.

name "Bamboo Flute";
author "Misha Nasledov";
description "A breathy transverse bamboo flute: a scoop into each note, a meri bend down, late vibrato and brightness with velocity.";
category "Leads and stabs";

    @breath = 0.5;
    @breath.widget = 1;
    @breath.min = 0;
    @breath.max = 2;
    @breath.label = "Breath";

    @meri = 0;
    @meri.widget = 1;
    @meri.min = 0;
    @meri.max = 3;
    @meri.label = "Meri (semitones)";

    @scoop = 0.6;
    @scoop.widget = 1;
    @scoop.min = 0;
    @scoop.max = 3;
    @scoop.label = "Scoop (semitones)";

    @scooptime = 90 ms;
    @scooptime.widget = 1;
    @scooptime.min = 10ms;
    @scooptime.max = 500ms;
    @scooptime.label = "Scoop Time";

    @vibrato = 18;
    @vibrato.widget = 1;
    @vibrato.min = 0;
    @vibrato.max = 80;
    @vibrato.label = "Vibrato (cents)";

    @vibrate = 5.2;
    @vibrate.widget = 1;
    @vibrate.min = 0.5;
    @vibrate.max = 10;
    @vibrate.label = "Vibrato Rate (Hz)";

    @vibdelay = 450 ms;
    @vibdelay.widget = 1;
    @vibdelay.min = 0;
    @vibdelay.max = 2000ms;
    @vibdelay.label = "Vibrato Delay";

    @attack = 60 ms;
    @attack.widget = 1;
    @attack.min = 5ms;
    @attack.max = 1000ms;
    @attack.label = "Attack";

    @release = 140 ms;
    @release.widget = 1;
    @release.min = 80ms;
    @release.max = 2000ms;
    @release.label = "Release";

    @level = 1;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 2;
    @level.label = "Level";

node ionode {
    channels = 2;
    out0 = mix->out;
    out1 = mix->out;
    play = env->play;
};

node scoopenv env::ad {
    a = 0;
    d = @scooptime;
};

# A knob moves once a window, so the bend is glided rather than stepped.
node meri misc::slew {
    in = @meri;
    time = 40 ms;
};

node freq misc::midi2freq {
    note = ionode->note - @scoop * scoopenv->out - meri->out;
};

node vib misc::vibrato {
    in = freq->out;
    rate = @vibrate;
    depth = @vibrato;
    delay = @vibdelay;
    rise = @vibdelay;
};

node fund osc::simple {
    freq = vib->out;
    waveform = 0;
};

node octave osc::simple {
    freq = vib->out * 2;
    waveform = 0;
};

node edge osc::blep {
    freq = vib->out;
    waveform = 0;
};

# The floor keeps a low note's harmonics, which is all of it that is
# heard, and the ceiling keeps the filter under its limit at the top.
node tone filt::svf {
    in = edge->out;
    cutoff = clamp(vib->out * (2.5 + 3 * ionode->velocity * ionode->velocity) *
                   exp2(0 - meri->out * 0.5), 400, 16000);
    res = 0.2;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node hollow filt::svf {
    in = noise->out;
    cutoff = clamp(vib->out, 150, 12000);
    res = 0.7;
};

node hiss filt::svf {
    in = noise->out;
    cutoff = 3000;
    res = 0;
};

node chiff env::ad {
    a = 3 ms;
    d = 70 ms;
    p = ionode->velocity;
};

node env env::adsr {
    a = @attack * (1.5 - ionode->velocity);
    d = 200 ms;
    s = 0.85 * (0.35 + 0.65 * ionode->velocity);
    r = @release;
    p = 0.35 + 0.65 * ionode->velocity;
    trigger = ionode->trigger;
};

node air math::add {
    in0 = hollow->out_band * 1.2 * @breath;
    in1 = hiss->out_high * 0.2 * @breath;
};

# The saw's fundamental is opposite in phase to the sine's, so it is
# subtracted: added, it would cancel the note it is meant to brighten.
# The chiff is outside the envelope, so it is heard before the tone is.
node mix math::mul {
    in0 = (fund->out +
           octave->out * (0.08 + 0.12 * ionode->velocity * ionode->velocity) *
           clamp((10000 - vib->out) / 1000, 0, 1) +
           tone->out_low * (0.45 + 0.6 * ionode->velocity * ionode->velocity) * -1 +
           air->out) *
          env->out * (1 + (vib->out / freq->out - 1) * 6) *
          (1 - meri->out * 0.08) +
          air->out * chiff->out * 2;
    in1 = @level * 0.2;
};

io ionode;
