# Horns -- a four-piece horn section, as on a funk or disco record.
#
# brass.dsp is one synth brass voice. This is four players on the note,
# and each does what a horn player does and a synth does not: comes in
# from under the pitch -- `Scoop' cents flat, rising over `Scoop Time'
# -- and gets brighter the harder it blows, its filter opening with its
# own envelope by `Bite' times the velocity. The brightness is a
# multiple of the note, so the timbre is the same up the range.
#
# THE SECTION IS LOOSE ON PURPOSE. Each player is `Detune' cents from
# the others and drifting, they come in up to `Stagger' after the key,
# each scooping from its own entry, their vibratos run at different
# rates, and they stand across the stereo field by `Width'. A section
# that hit together to the sample would be one horn played loud.

name "Horns";
author "Misha Nasledov";
description "Four brass players a note, each scooping up into it, brightening with velocity, entering a little apart and seated across the stereo field.";
category "Leads and stabs";

    @detune = 6;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 30;
    @detune.label = "Detune (cents)";
    @detune.group = "Players";

    @drift = 3;
    @drift.widget = 1;
    @drift.min = 0;
    @drift.max = 20;
    @drift.label = "Drift (cents)";
    @drift.group = "Players";

    @stagger = 40 ms;
    @stagger.widget = 1;
    @stagger.min = 0 ms;
    @stagger.max = 200 ms;
    @stagger.label = "Stagger";
    @stagger.group = "Players";

    @width = 0.8;
    @width.widget = 1;
    @width.min = 0;
    @width.max = 1;
    @width.label = "Width";
    @width.group = "Players";

    @scoop = 40;
    @scoop.widget = 1;
    @scoop.min = 0;
    @scoop.max = 200;
    @scoop.label = "Scoop (cents)";
    @scoop.group = "Players";

    @scooptime = 60 ms;
    @scooptime.widget = 1;
    @scooptime.min = 5 ms;
    @scooptime.max = 400 ms;
    @scooptime.label = "Scoop Time";
    @scooptime.group = "Players";

    @bite = 8;
    @bite.widget = 1;
    @bite.min = 0;
    @bite.max = 20;
    @bite.label = "Bite";
    @bite.group = "Tone";

    @vibrato = 8;
    @vibrato.widget = 1;
    @vibrato.min = 0;
    @vibrato.max = 50;
    @vibrato.label = "Vibrato (cents)";
    @vibrato.group = "Vibrato";

    @vibrate = 5.2;
    @vibrate.widget = 1;
    @vibrate.min = 3;
    @vibrate.max = 8;
    @vibrate.label = "Vibrato Rate (Hz)";
    @vibrate.group = "Vibrato";

    @vibdelay = 450 ms;
    @vibdelay.widget = 1;
    @vibdelay.min = 0 ms;
    @vibdelay.max = 2000 ms;
    @vibdelay.label = "Vibrato Delay";
    @vibdelay.group = "Vibrato";

    @a = 25 ms;
    @a.widget = 1;
    @a.min = 1 ms;
    @a.max = 1000 ms;
    @a.label = "Attack";
    @a.group = "Envelope";

    @d = 250 ms;
    @d.widget = 1;
    @d.min = 0 ms;
    @d.max = 3000 ms;
    @d.label = "Decay";
    @d.group = "Envelope";

    @s = 0.75;
    @s.widget = 1;
    @s.min = 0.01;
    @s.max = 1;
    @s.label = "Sustain";
    @s.group = "Envelope";

    @r = 160 ms;
    @r.widget = 1;
    @r.min = 70 ms;
    @r.max = 3000 ms;
    @r.label = "Release";
    @r.group = "Envelope";

node ionode {
    channels = 2;
    out0 = outl->out;
    out1 = outr->out;
    play = max(max(env1->play, env2->play), max(env3->play, env4->play));
};

node freq misc::midi2freq {
    note = ionode->note;
};

# Player 1.
# Player 1 comes in `Stagger' times 0.0 after the key: a timer that
# opens its gate then, and shuts it with the key.
node wait1 env::adsr {
    a = @stagger * 0.0;  d = 0;  s = 1;  r = 0;
    trigger = ionode->trigger;
};
node gate1 math::clamp { in = (wait1->out - 0.999) * 1000; lo = 0; hi = 1; };
node drift1 misc::drift {
    rate = 0.3;  depth = @drift;  seed = ionode->note + 1000;
};
# From `Scoop' cents flat to the note over `Scoop Time', from the entry.
# A sustain just over 0, since an env::adsr that decays to 0 restarts.
node scoop1 env::adsr {
    a = 0;  d = @scooptime;  s = 0.001;  r = 0;
    trigger = gate1->out;
};
node vib1 misc::vibrato {
    in = freq->out * exp2((drift1->out + @detune * -1.0 -
                           @scoop * scoop1->out) / 1200);
    rate = @vibrate * 0.94;
    depth = @vibrato;
    delay = @vibdelay + @stagger * 0.0;
    rise = @vibdelay;
};
node osc1 osc::blep { freq = vib1->out; waveform = 0; phase = 0.0; };
node fenv1 env::adsr {
    a = @a;
    d = @d;
    s = 0.6;
    r = @r;
    trigger = gate1->out;
};
node tone1 filt::svf {
    in = osc1->out;
    cutoff = min(freq->out * (1 + fenv1->out * @bite * ionode->velocity), 16000);
    res = 0.2;
};
node env1 env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = gate1->out;
};
node seat1 mixer::pan { in = tone1->out_low * env1->out; pan = -0.8 * @width; };

# Player 2.
# Player 2 comes in `Stagger' times 0.6 after the key: a timer that
# opens its gate then, and shuts it with the key.
node wait2 env::adsr {
    a = @stagger * 0.6;  d = 0;  s = 1;  r = 0;
    trigger = ionode->trigger;
};
node gate2 math::clamp { in = (wait2->out - 0.999) * 1000; lo = 0; hi = 1; };
node drift2 misc::drift {
    rate = 0.3;  depth = @drift;  seed = ionode->note + 2000;
};
# From `Scoop' cents flat to the note over `Scoop Time', from the entry.
# A sustain just over 0, since an env::adsr that decays to 0 restarts.
node scoop2 env::adsr {
    a = 0;  d = @scooptime;  s = 0.001;  r = 0;
    trigger = gate2->out;
};
node vib2 misc::vibrato {
    in = freq->out * exp2((drift2->out + @detune * -0.33 -
                           @scoop * scoop2->out) / 1200);
    rate = @vibrate * 1.03;
    depth = @vibrato;
    delay = @vibdelay + @stagger * 0.6;
    rise = @vibdelay;
};
node osc2 osc::blep { freq = vib2->out; waveform = 0; phase = 0.6; };
node fenv2 env::adsr {
    a = @a;
    d = @d;
    s = 0.6;
    r = @r;
    trigger = gate2->out;
};
node tone2 filt::svf {
    in = osc2->out;
    cutoff = min(freq->out * (1 + fenv2->out * @bite * ionode->velocity), 16000);
    res = 0.2;
};
node env2 env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = gate2->out;
};
node seat2 mixer::pan { in = tone2->out_low * env2->out; pan = -0.3 * @width; };

# Player 3.
# Player 3 comes in `Stagger' times 0.25 after the key: a timer that
# opens its gate then, and shuts it with the key.
node wait3 env::adsr {
    a = @stagger * 0.25;  d = 0;  s = 1;  r = 0;
    trigger = ionode->trigger;
};
node gate3 math::clamp { in = (wait3->out - 0.999) * 1000; lo = 0; hi = 1; };
node drift3 misc::drift {
    rate = 0.3;  depth = @drift;  seed = ionode->note + 3000;
};
# From `Scoop' cents flat to the note over `Scoop Time', from the entry.
# A sustain just over 0, since an env::adsr that decays to 0 restarts.
node scoop3 env::adsr {
    a = 0;  d = @scooptime;  s = 0.001;  r = 0;
    trigger = gate3->out;
};
node vib3 misc::vibrato {
    in = freq->out * exp2((drift3->out + @detune * 0.33 -
                           @scoop * scoop3->out) / 1200);
    rate = @vibrate * 0.98;
    depth = @vibrato;
    delay = @vibdelay + @stagger * 0.25;
    rise = @vibdelay;
};
node osc3 osc::blep { freq = vib3->out; waveform = 0; phase = 0.25; };
node fenv3 env::adsr {
    a = @a;
    d = @d;
    s = 0.6;
    r = @r;
    trigger = gate3->out;
};
node tone3 filt::svf {
    in = osc3->out;
    cutoff = min(freq->out * (1 + fenv3->out * @bite * ionode->velocity), 16000);
    res = 0.2;
};
node env3 env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = gate3->out;
};
node seat3 mixer::pan { in = tone3->out_low * env3->out; pan = 0.3 * @width; };

# Player 4.
# Player 4 comes in `Stagger' times 0.9 after the key: a timer that
# opens its gate then, and shuts it with the key.
node wait4 env::adsr {
    a = @stagger * 0.9;  d = 0;  s = 1;  r = 0;
    trigger = ionode->trigger;
};
node gate4 math::clamp { in = (wait4->out - 0.999) * 1000; lo = 0; hi = 1; };
node drift4 misc::drift {
    rate = 0.3;  depth = @drift;  seed = ionode->note + 4000;
};
# From `Scoop' cents flat to the note over `Scoop Time', from the entry.
# A sustain just over 0, since an env::adsr that decays to 0 restarts.
node scoop4 env::adsr {
    a = 0;  d = @scooptime;  s = 0.001;  r = 0;
    trigger = gate4->out;
};
node vib4 misc::vibrato {
    in = freq->out * exp2((drift4->out + @detune * 1.0 -
                           @scoop * scoop4->out) / 1200);
    rate = @vibrate * 1.07;
    depth = @vibrato;
    delay = @vibdelay + @stagger * 0.9;
    rise = @vibdelay;
};
node osc4 osc::blep { freq = vib4->out; waveform = 0; phase = 0.9; };
node fenv4 env::adsr {
    a = @a;
    d = @d;
    s = 0.6;
    r = @r;
    trigger = gate4->out;
};
node tone4 filt::svf {
    in = osc4->out;
    cutoff = min(freq->out * (1 + fenv4->out * @bite * ionode->velocity), 16000);
    res = 0.2;
};
node env4 env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = gate4->out;
};
node seat4 mixer::pan { in = tone4->out_low * env4->out; pan = 0.8 * @width; };

node suml math::add {
    in0 = seat1->out0 + seat2->out0;
    in1 = seat3->out0 + seat4->out0;
};
node sumr math::add {
    in0 = seat1->out1 + seat2->out1;
    in1 = seat3->out1 + seat4->out1;
};

node outl math::mul { in0 = suml->out; in1 = 0.18; };
node outr math::mul { in0 = sumr->out; in1 = 0.18; };

io ionode;
