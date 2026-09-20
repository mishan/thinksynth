# Kit Snare -- a shell, a head and forty wires.
#
# snare808.dsp is two tuned sines and a hiss, which is what the circuit
# was. A drum is a shell with a head stretched over each end and a set
# of wires lying against the bottom one, and the three of them decay at
# different rates -- which is the whole reason a real snare reads as a
# drum and a hiss through an envelope reads as a machine.
#
#   THE SHELL, two partials a fifth apart at `Tune'. The upper one goes
#   first, because a shell's higher modes are the ones the air and the
#   wood damp fastest. Equal decays are a bell.
#
#   THE HEAD, a band the stick rings at about twice the shell, ringing
#   on past the click that started it. This is the part that says how
#   big the drum is.
#
#   THE WIRES, two noise bands at `Snare' and four times it, the upper
#   one shorter. Wires are not a hiss: they are a rattle whose brightest
#   part dies first as they settle back against the head.
#
# VELOCITY IS IN THE WIRES more than in the shell, and that is what a
# ghost note is. A drummer playing at a tenth of full stroke barely
# moves the head but still rattles the wires, so a quiet hit on this
# graph is mostly snares and a loud one is mostly drum. A fader cannot
# do that, and it is why a snare line written with dynamics sounds
# played.
#
# The excitation is an env::ad click rather than an `impulse::' node --
# see rim808.dsp for the window-length reason -- and the resonances are
# filt::svf, whose cutoff is in hertz and exact, rather than
# filt::resonator, whose `freq' is an allpass coefficient and not the
# pitch it rings at.

name "Kit Snare";
author "Misha Nasledov";
description "Two shell partials, a head and two bands of wire, with velocity in the rattle: the acoustic snare.";

    @tune = 185;
    @tune.widget = 1;
    @tune.min = 90;
    @tune.max = 400;
    @tune.label = "Tune (Hz)";

    @ratio = 1.5;
    @ratio.widget = 1;
    @ratio.min = 1;
    @ratio.max = 3;
    @ratio.label = "Second Partial";

    @sd = 140 ms;
    @sd.widget = 1;
    @sd.min = 20ms;
    @sd.max = 800ms;
    @sd.label = "Shell Decay";

    @head = 2.1;
    @head.widget = 1;
    @head.min = 1;
    @head.max = 6;
    @head.label = "Head (x Tune)";

    @hd = 60 ms;
    @hd.widget = 1;
    @hd.min = 5ms;
    @hd.max = 500ms;
    @hd.label = "Head Decay";

    @snare = 1900;
    @snare.widget = 1;
    @snare.min = 500;
    @snare.max = 6000;
    @snare.label = "Wires (Hz)";

    @wires = 0.7;
    @wires.widget = 1;
    @wires.min = 0;
    @wires.max = 1.5;
    @wires.label = "Wire Level";

    @wd = 170 ms;
    @wd.widget = 1;
    @wd.min = 20ms;
    @wd.max = 1200ms;
    @wd.label = "Wire Decay";

    @ghost = 0.6;
    @ghost.widget = 1;
    @ghost.min = 0;
    @ghost.max = 1;
    @ghost.label = "Ghost";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = wenv->play;
};

# The stick: two milliseconds of noise, which excites everything below
# and is heard as the crack on the very front.
node stick env::ad {
    a = 0;
    d = 2 ms;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node hit mixer::mul {
    in0 = noise->out;
    in1 = stick->out;
};

# The shell, and the reason there are two of them: one sine is a tom.
node low osc::simple {
    freq = @tune;
    waveform = 0;
};

node high osc::simple {
    freq = @tune * @ratio;
    waveform = 0;
};

node lenv env::ad {
    a = 0;
    d = @sd;
};

# Half the shell's decay, so the interval between the two partials is
# heard at the hit and gone by the middle of the note.
node henv env::ad {
    a = 0;
    d = @sd * 0.5;
};

# The head: a narrow band rung by the stick and left to ring.
node skin filt::svf {
    in = hit->out * 0.6;
    cutoff = @tune * @head;
    res = 0.96;
};

node skenv env::ad {
    a = 0;
    d = @hd;
};

# The wires. Two bands, the upper one a quarter shorter than the lower:
# what settles first is the brightest part of the rattle.
node wire1 filt::svf {
    in = noise->out;
    cutoff = @snare;
    res = 0.2;
};

node wire2 filt::svf {
    in = noise->out;
    cutoff = @snare * 4;
    res = 0.2;
};

node wenv env::ad {
    a = 0.5 ms;
    d = @wd;
};

node wenv2 env::ad {
    a = 0.5 ms;
    d = @wd * 0.45;
};

# The ghost: `Ghost' at 0 is a shell scaled by velocity like anything
# else, and up it leans the quiet strokes towards the wires -- a light
# stroke rattles the snares without moving the head.
node shell math::mul {
    in0 = low->out * lenv->out * 0.5 + high->out * henv->out * 0.35 +
          skin->out_band * skenv->out * 0.8;
    in1 = ionode->velocity * (1 - @ghost) + ionode->velocity *
          ionode->velocity * @ghost;
};

node rattle math::mul {
    in0 = wire1->out_band * wenv->out + wire2->out_band * wenv2->out * 0.7;
    in1 = ionode->velocity * @wires;
};

node out math::add {
    in0 = shell->out;
    in1 = rattle->out;
};

io ionode;
