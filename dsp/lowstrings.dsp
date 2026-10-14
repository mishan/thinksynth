# Low Strings -- cellos and basses playing short, for an ostinato.
#
# section.dsp is the section bowing long notes: the bow takes time, the
# filter opens with it, the vibrato comes late. None of that is any use
# to a part in sixteenths, which is most of what low strings play under
# a film or a game: a short stroke that bites at the front and is gone.
# The sampled cellos do it, from a pack a fresh build does not have.
# This is the same gesture from oscillators.
#
# THE BITE. The filter opens on its own short envelope, `Bite' wide and
# `Bite Time' long, harder the harder the note is played, and closes
# again while the note is still sounding -- so the front of the stroke is
# brighter than its middle, which is a bow digging in and letting go.
# Under it, a scrape of noise as long as the bite: rosin on the string.
#
# THE BODY. A cello is a box, and the box has resonances that stay put
# whatever note is played -- the air in it low, the wood higher. Two
# bands at fixed frequencies, `Body' loud, are what make three saws a
# string section rather than a synth bass.
#
# THREE PLAYERS. Three saws, `Detune' cents apart, and a chorus either
# side as section.dsp has, `Width' wide.

name "Low Strings";
author "Misha Nasledov";
description "Spiccato cellos and basses: a stroke that bites and lets go, over a body that stays put. For ostinatos.";
category "Strings and pads";

    @detune = 7;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 30;
    @detune.label = "Detune (cents)";

    @cutoff = 420;
    @cutoff.widget = 1;
    @cutoff.min = 100;
    @cutoff.max = 3000;
    @cutoff.label = "Cutoff (Hz)";

    @bite = 2800;
    @bite.widget = 1;
    @bite.min = 0;
    @bite.max = 8000;
    @bite.label = "Bite (Hz)";

    @bitet = 90 ms;
    @bitet.widget = 1;
    @bitet.min = 10ms;
    @bitet.max = 500ms;
    @bitet.label = "Bite Time";

    @rosin = 0.25;
    @rosin.widget = 1;
    @rosin.min = 0;
    @rosin.max = 1;
    @rosin.label = "Rosin";

    @body = 0.6;
    @body.widget = 1;
    @body.min = 0;
    @body.max = 1.5;
    @body.label = "Body";

    @width = 0.5;
    @width.widget = 1;
    @width.min = 0;
    @width.max = 1;
    @width.label = "Width";

    @a = 6 ms;
    @a.widget = 1;
    @a.min = 1ms;
    @a.max = 300ms;
    @a.label = "Attack";

    @d = 180 ms;
    @d.widget = 1;
    @d.min = 10ms;
    @d.max = 2000ms;
    @d.label = "Decay";

    @s = 0.45;
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 140 ms;
    @r.widget = 1;
    @r.min = 10ms;
    @r.max = 2000ms;
    @r.label = "Release";

node ionode {
    channels = 2;

    # Sixteenths ring into each other through the release.
    poly = 8;

    out0 = left->out;
    out1 = right->out;
    play = env->play;
};

node note misc::midi2freq {
    note = ionode->note;
};

node osc1 osc::simple {
    freq = note->out;
    waveform = 1;
};

node osc2 osc::simple {
    freq = note->out * exp2(@detune / 1200);
    waveform = 1;
};

node osc3 osc::simple {
    freq = note->out * exp2(0 - @detune / 1200);
    waveform = 1;
};

node bitenv env::ad {
    a = 1 ms;
    d = @bitet;
};

node noise osc::noise {
    color = 1;
    amp = 1;
};

node scrape filt::svf {
    in = noise->out * bitenv->out;
    cutoff = 2400;
    res = 0.2;
};

node tone filt::svf {
    in = (osc1->out + osc2->out + osc3->out) * 0.33 +
         scrape->out_band * @rosin * ionode->velocity;
    cutoff = @cutoff + bitenv->out * @bite * ionode->velocity;
    res = 0.2;
};

# The box: its air, and its wood.
node air filt::svf {
    in = tone->out_low;
    cutoff = 230;
    res = 0.7;
};

node wood filt::svf {
    in = tone->out_low;
    cutoff = 780;
    res = 0.6;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node voiced math::mul {
    in0 = tone->out_low * 0.7 + air->out_band * @body * 0.8 +
          wood->out_band * @body * 0.5;
    in1 = env->out;
};

node wide delay::chorus {
    in = voiced->out;
    rate = 0.5;
    depth = 4 ms;
    delay = 10 ms;
    taps = 3;
    mix = 1;
    phase = 0;
};

node wide2 delay::chorus {
    in = voiced->out;
    rate = 0.5;
    depth = 4 ms;
    delay = 10 ms;
    taps = 3;
    mix = 1;
    phase = 0.5;
};

node left math::add {
    in0 = voiced->out * (1 - @width) * 0.8;
    in1 = wide->out * @width * 0.8;
};

node right math::add {
    in0 = voiced->out * (1 - @width) * 0.8;
    in1 = wide2->out * @width * 0.8;
};

io ionode;
