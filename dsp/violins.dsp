# Violins -- four desks, each its own player.
#
# strings.dsp and section.dsp each make one voice sound like many with a
# chorus. This one is four: four sawtooth players on every note, each a
# few cents from the others by `Detune', each wandering by `Drift', each
# with a vibrato at its own rate that it leans into late, and each
# coming in `Stagger' after the one before -- bows do not touch the
# string at once. They sit across the stage by `Width', first desks
# left, seconds right.
#
# THE BODY IS SHARED. What makes a saw a violin is less the string than
# the box: a wooden resonance near 290 Hz and the bridge's hill around
# 2.8 kHz. Every instrument in a section has them, so they are applied
# once to each side of the summed section rather than per player, by
# `Body'. The tone filter before it tracks the note and opens with
# velocity, a harder bow being a brighter one.

name "Violins";
author "Misha Nasledov";
description "Four detuned, drifting players a note, each with its own vibrato and entry, seated across the stereo field through a violin body.";
category "Strings and pads";

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

    @vibrato = 14;
    @vibrato.widget = 1;
    @vibrato.min = 0;
    @vibrato.max = 50;
    @vibrato.label = "Vibrato (cents)";
    @vibrato.group = "Vibrato";

    @vibrate = 5.6;
    @vibrate.widget = 1;
    @vibrate.min = 3;
    @vibrate.max = 8;
    @vibrate.label = "Vibrato Rate (Hz)";
    @vibrate.group = "Vibrato";

    @vibdelay = 350 ms;
    @vibdelay.widget = 1;
    @vibdelay.min = 0 ms;
    @vibdelay.max = 2000 ms;
    @vibdelay.label = "Vibrato Delay";
    @vibdelay.group = "Vibrato";

    @bright = 6;
    @bright.widget = 1;
    @bright.min = 1;
    @bright.max = 16;
    @bright.label = "Brightness";
    @bright.group = "Tone";

    @body = 0.8;
    @body.widget = 1;
    @body.min = 0;
    @body.max = 2;
    @body.label = "Body";
    @body.group = "Tone";

    @a = 180 ms;
    @a.widget = 1;
    @a.min = 5 ms;
    @a.max = 3000 ms;
    @a.label = "Attack";
    @a.group = "Envelope";

    @d = 300 ms;
    @d.widget = 1;
    @d.min = 0 ms;
    @d.max = 3000 ms;
    @d.label = "Decay";
    @d.group = "Envelope";

    @s = 0.85;
    @s.widget = 1;
    @s.min = 0.01;
    @s.max = 1;
    @s.label = "Sustain";
    @s.group = "Envelope";

    @r = 450 ms;
    @r.widget = 1;
    @r.min = 70 ms;
    @r.max = 4000 ms;
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
node drift1 misc::drift {
    rate = 0.25;  depth = @drift;  seed = ionode->note + 1000;
};
node vib1 misc::vibrato {
    in = freq->out * exp2((drift1->out + @detune * -1.0) / 1200);
    rate = @vibrate * 0.94;
    depth = @vibrato;
    delay = @vibdelay + @stagger * 0.0;
    rise = @vibdelay;
};
node osc1 osc::blep { freq = vib1->out; waveform = 0; phase = 0.0; };
node tone1 filt::svf {
    in = osc1->out;
    cutoff = min(freq->out * @bright * (0.6 + 0.4 * ionode->velocity), 16000);
    res = 0.1;
};
node env1 env::adsr {
    a = @a + @stagger * 0.0;
    d = @d;
    s = @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};
node seat1 mixer::pan { in = tone1->out_low * env1->out; pan = -0.8 * @width; };

# Player 2.
node drift2 misc::drift {
    rate = 0.25;  depth = @drift;  seed = ionode->note + 2000;
};
node vib2 misc::vibrato {
    in = freq->out * exp2((drift2->out + @detune * -0.33) / 1200);
    rate = @vibrate * 1.03;
    depth = @vibrato;
    delay = @vibdelay + @stagger * 0.6;
    rise = @vibdelay;
};
node osc2 osc::blep { freq = vib2->out; waveform = 0; phase = 0.6; };
node tone2 filt::svf {
    in = osc2->out;
    cutoff = min(freq->out * @bright * (0.6 + 0.4 * ionode->velocity), 16000);
    res = 0.1;
};
node env2 env::adsr {
    a = @a + @stagger * 0.6;
    d = @d;
    s = @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};
node seat2 mixer::pan { in = tone2->out_low * env2->out; pan = -0.3 * @width; };

# Player 3.
node drift3 misc::drift {
    rate = 0.25;  depth = @drift;  seed = ionode->note + 3000;
};
node vib3 misc::vibrato {
    in = freq->out * exp2((drift3->out + @detune * 0.33) / 1200);
    rate = @vibrate * 0.98;
    depth = @vibrato;
    delay = @vibdelay + @stagger * 0.25;
    rise = @vibdelay;
};
node osc3 osc::blep { freq = vib3->out; waveform = 0; phase = 0.25; };
node tone3 filt::svf {
    in = osc3->out;
    cutoff = min(freq->out * @bright * (0.6 + 0.4 * ionode->velocity), 16000);
    res = 0.1;
};
node env3 env::adsr {
    a = @a + @stagger * 0.25;
    d = @d;
    s = @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};
node seat3 mixer::pan { in = tone3->out_low * env3->out; pan = 0.3 * @width; };

# Player 4.
node drift4 misc::drift {
    rate = 0.25;  depth = @drift;  seed = ionode->note + 4000;
};
node vib4 misc::vibrato {
    in = freq->out * exp2((drift4->out + @detune * 1.0) / 1200);
    rate = @vibrate * 1.07;
    depth = @vibrato;
    delay = @vibdelay + @stagger * 0.9;
    rise = @vibdelay;
};
node osc4 osc::blep { freq = vib4->out; waveform = 0; phase = 0.9; };
node tone4 filt::svf {
    in = osc4->out;
    cutoff = min(freq->out * @bright * (0.6 + 0.4 * ionode->velocity), 16000);
    res = 0.1;
};
node env4 env::adsr {
    a = @a + @stagger * 0.9;
    d = @d;
    s = @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
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

node bodyl1 filt::svf { in = suml->out; cutoff = 290; res = 0.6; };
node bodyl2 filt::svf { in = suml->out; cutoff = 2800; res = 0.4; };
node bodyr1 filt::svf { in = sumr->out; cutoff = 290; res = 0.6; };
node bodyr2 filt::svf { in = sumr->out; cutoff = 2800; res = 0.4; };

node outl math::mul {
    in0 = suml->out + (bodyl1->out_band + bodyl2->out_band * 0.8) * @body;
    in1 = 0.11;
};
node outr math::mul {
    in0 = sumr->out + (bodyr1->out_band + bodyr2->out_band * 0.8) * @body;
    in1 = 0.11;
};

io ionode;
