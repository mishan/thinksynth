# Section -- players, not a string machine.
#
# strings.dsp is a Solina: a divide-down organ through a bucket-brigade
# ensemble, and what makes it an instrument is the box at the end. This
# is the other seventies string sound, the one arranged for and played
# by people, and every difference between them is a difference of
# gesture:
#
#   THE BOW TAKES TIME. A section does not start: it arrives. The amp
#   envelope's attack is a couple of hundred milliseconds and the filter
#   opens *with* it, so the note gets brighter as it gets louder, which
#   is a bow biting rather than a filter sweeping. One envelope on both
#   is the point -- a fast attack under a slow filter is a synth pad,
#   and the two moving together is a string section.
#
#   THE VIBRATO ARRIVES LATE. A player holds a note straight and then
#   leans on it: `misc::vibrato' has the delay and the rise for exactly
#   that, and the delay is what keeps a section from sounding like an
#   LFO. Short notes never reach it at all, which is also true of the
#   players.
#
#   THE CHORUS IS INSIDE THE VOICE, three taps of it. That is
#   strings.dsp's argument and it applies here for the same reason: a
#   chorus on the channel's sum moves every note of a chord together,
#   which is one instrument wobbling, and a chorus per voice puts each
#   note on its own -- which is what a section is. Three taps rather
#   than strings.dsp's two banks of three, because a section is a dozen
#   players and not a machine imitating four hundred.
#
# `Swoop' is the slide into the note, in semitones, over `Swoop Time'.
# At a few tens of milliseconds it is the bow catching the string; at a
# second and a half it is the octave slide a section plays into a
# chorus. It is per note and not a portamento between two -- this graph
# is polyphonic, because a section plays chords -- so the line that
# swoops wants its own channel, which is how the part is written anyway.

name "Section";
author "Misha Nasledov";
description "Bowed strings: a filter that opens with the bow, vibrato that arrives late, and a chorus per voice.";

    @detune = 9;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 40;
    @detune.label = "Detune (cents)";

    @cutoff = 380;
    @cutoff.widget = 1;
    @cutoff.min = 100;
    @cutoff.max = 4000;
    @cutoff.label = "Cutoff (Hz)";

    @depth = 2600;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 9000;
    @depth.label = "Bow Depth (Hz)";

    @res = 0.15;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Resonance";

    @vrate = 5.4;
    @vrate.widget = 1;
    @vrate.min = 0;
    @vrate.max = 12;
    @vrate.label = "Vibrato (Hz)";

    @vdepth = 22;
    @vdepth.widget = 1;
    @vdepth.min = 0;
    @vdepth.max = 100;
    @vdepth.label = "Vibrato Depth (cents)";

    @vdelay = 400 ms;
    @vdelay.widget = 1;
    @vdelay.min = 0;
    @vdelay.max = 3000ms;
    @vdelay.label = "Vibrato Delay";

    @vrise = 500 ms;
    @vrise.widget = 1;
    @vrise.min = 0;
    @vrise.max = 3000ms;
    @vrise.label = "Vibrato Rise";

    @swoop = 0;
    @swoop.widget = 1;
    @swoop.min = 0;
    @swoop.max = 12;
    @swoop.label = "Swoop (semitones)";

    @swoopt = 120 ms;
    @swoopt.widget = 1;
    @swoopt.min = 5ms;
    @swoopt.max = 3000ms;
    @swoopt.label = "Swoop Time";

    @width = 0.6;
    @width.widget = 1;
    @width.min = 0;
    @width.max = 1;
    @width.label = "Width";

    @a = 220 ms;
    @a.widget = 1;
    @a.min = 1ms;
    @a.max = 2000ms;
    @a.label = "Attack";

    @d = 400 ms;
    @d.widget = 1;
    @d.min = 10ms;
    @d.max = 3000ms;
    @d.label = "Decay";

    @s = 0.85;
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 420 ms;
    @r.widget = 1;
    @r.min = 10ms;
    @r.max = 4000ms;
    @r.label = "Release";

node ionode {
    channels = 2;

    # A section plays chords, and the release of one is under the attack
    # of the next.
    poly = 8;

    out0 = left->out;
    out1 = right->out;
    play = env->play;
};

node note misc::midi2freq {
    note = ionode->note;
};

# The slide into the note: 1 at the attack and 0 once `Swoop Time' is
# over, so the pitch starts `Swoop' semitones under and arrives.
node scoop env::ad {
    a = 0;
    d = @swoopt;
};

node freq math::mul {
    in0 = note->out;
    in1 = exp2(0 - @swoop * scoop->out / 12);
};

# Held straight and then leaned on. A note shorter than `Vibrato Delay'
# never reaches it, which is what a player does with a short one.
node vib misc::vibrato {
    in = freq->out;
    rate = @vrate;
    depth = @vdepth;
    delay = @vdelay;
    rise = @vrise;
};

node osc1 osc::simple {
    freq = vib->out * exp2(@detune / 1200);
    waveform = 1;
};

node osc2 osc::simple {
    freq = vib->out * exp2(0 - @detune / 1200);
    waveform = 1;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

# The bow: the same envelope that is the level, so the note brightens as
# it arrives and dulls as it goes. Velocity is in the depth as well,
# because a section playing quietly is playing with less bow and not
# only further from the microphone.
node tone filt::svf {
    in = osc1->out * 0.5 + osc2->out * 0.5;
    cutoff = @cutoff + env->out * @depth * ionode->velocity;
    res = @res;
};

node wide delay::chorus {
    in = tone->out_low;
    rate = 0.45;
    depth = 5 ms;
    delay = 11 ms;
    taps = 3;
    mix = 1;
    phase = 0;
};

# The other side of the room, reading the same line half a cycle away:
# two of these is what makes a section wide rather than merely wobbly.
node wide2 delay::chorus {
    in = tone->out_low;
    rate = 0.45;
    depth = 5 ms;
    delay = 11 ms;
    taps = 3;
    mix = 1;
    phase = 0.5;
};

node left mixer::mul {
    in0 = tone->out_low * (1 - @width) * 0.7 + wide->out * @width * 0.7;
    in1 = env->out;
};

node right mixer::mul {
    in0 = tone->out_low * (1 - @width) * 0.7 + wide2->out * @width * 0.7;
    in1 = env->out;
};

io ionode;
