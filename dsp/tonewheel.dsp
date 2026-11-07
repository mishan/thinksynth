# Tonewheel -- nine drawbars, a key click, percussion and the scanner.
#
# A Hammond is additive synthesis done with gears: a sine for every
# drawbar, each at a fixed ratio to the key -- 16' an octave down, 5 1/3'
# a fifth up, 8' the note, then 4', 2 2/3', 2', 1 3/5', 1 1/3' and 1'
# at the 2nd, 3rd, 4th, 5th, 6th and 8th harmonics. The drawbars run 0
# to 8 and each step is 3 dB, which is why `888000000' is the jazz
# sound and `888888888' is a wall: the scale is a level, not a mix.
#
# THE TOP FOLDS BACK. The highest tonewheel turns at about 5.9 kHz, so
# a footage that would land above it on a high key plays an octave
# lower instead. That is part of why a Hammond is never harsh up top,
# and it keeps the 1' bar on a high note from aliasing here.
#
# THE CLICK IS THE CONTACTS. Nine springs touch nine busbars a moment
# apart, and what comes out is a burst of every frequency at once --
# a few milliseconds of noise, which is what `Click' sets the level of.
#
# PERCUSSION is a second or third harmonic struck and left to decay, the
# piano-like attack of the B-3 in a rock band. On the instrument it
# fires only when every key was up; voices here are independent, so it
# strikes on every note, which is what a fast passage wants anyway.
#
# THE SCANNER is a delay line swept at about seven times a second: at
# `Chorus' 1 it is all wet (vibrato), at 0.5 the dry is mixed back in
# (chorus), and the beating between the two is the C3 setting
# everybody leaves it on. `Leakage' is the wheels nobody asked for --
# the faint other drawbars a real console bleeds.
#
# The overdrive and the rotating speaker are the channel's, not the
# voice's, because they act on the chord: put it on `fx/rotary.dsp' and
# turn up its `Drive'.

name "Tonewheel";
author "Misha Nasledov";
description "A drawbar organ: nine tonewheels with foldback, key click, percussion and scanner chorus.";
category "Keys";

    @db16 = 8;
    @db16.widget = 1;
    @db16.min = 0;
    @db16.max = 8;
    @db16.step = 1;
    @db16.label = "16'";
    @db16.group = "Drawbars";

    @db513 = 8;
    @db513.widget = 1;
    @db513.min = 0;
    @db513.max = 8;
    @db513.step = 1;
    @db513.label = "5 1/3'";
    @db513.group = "Drawbars";

    @db8 = 8;
    @db8.widget = 1;
    @db8.min = 0;
    @db8.max = 8;
    @db8.step = 1;
    @db8.label = "8'";
    @db8.group = "Drawbars";

    @db4 = 0;
    @db4.widget = 1;
    @db4.min = 0;
    @db4.max = 8;
    @db4.step = 1;
    @db4.label = "4'";
    @db4.group = "Drawbars";

    @db223 = 0;
    @db223.widget = 1;
    @db223.min = 0;
    @db223.max = 8;
    @db223.step = 1;
    @db223.label = "2 2/3'";
    @db223.group = "Drawbars";

    @db2 = 0;
    @db2.widget = 1;
    @db2.min = 0;
    @db2.max = 8;
    @db2.step = 1;
    @db2.label = "2'";
    @db2.group = "Drawbars";

    @db135 = 0;
    @db135.widget = 1;
    @db135.min = 0;
    @db135.max = 8;
    @db135.step = 1;
    @db135.label = "1 3/5'";
    @db135.group = "Drawbars";

    @db113 = 0;
    @db113.widget = 1;
    @db113.min = 0;
    @db113.max = 8;
    @db113.step = 1;
    @db113.label = "1 1/3'";
    @db113.group = "Drawbars";

    @db1 = 0;
    @db1.widget = 1;
    @db1.min = 0;
    @db1.max = 8;
    @db1.step = 1;
    @db1.label = "1'";
    @db1.group = "Drawbars";

    @perc = 0;
    @perc.widget = 1;
    @perc.min = 0;
    @perc.max = 1;
    @perc.label = "Percussion";
    @perc.group = "Percussion";

    @percharm = 3;
    @percharm.widget = 1;
    @percharm.min = 2;
    @percharm.max = 3;
    @percharm.step = 1;
    @percharm.label = "Harmonic";
    @percharm.group = "Percussion";

    @percdecay = 250 ms;
    @percdecay.widget = 1;
    @percdecay.min = 50 ms;
    @percdecay.max = 1500 ms;
    @percdecay.label = "Decay";
    @percdecay.group = "Percussion";

    @click = 0.3;
    @click.widget = 1;
    @click.min = 0;
    @click.max = 1;
    @click.label = "Key Click";

    @scanner = 0.6 ms;
    @scanner.widget = 1;
    @scanner.min = 0 ms;
    @scanner.max = 1.5 ms;
    @scanner.label = "Scanner Depth";
    @scanner.group = "Scanner";

    @vchorus = 0.5;
    @vchorus.widget = 1;
    @vchorus.min = 0;
    @vchorus.max = 1;
    @vchorus.label = "Chorus";
    @vchorus.group = "Scanner";

    @leak = 0.004;
    @leak.widget = 1;
    @leak.min = 0;
    @leak.max = 0.05;
    @leak.label = "Leakage";

    @r = 90 ms;
    @r.widget = 1;
    @r.min = 70 ms;
    @r.max = 1000 ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# One wheel per drawbar. A footage over the top wheel's 5920 Hz plays an
# octave down: `clamp((x - 5920) * 1000, 0, 1)' is "x is over 5920".
node w16  osc::simple { freq = freq->out * 0.5; waveform = 0; };
node w513 osc::simple { freq = freq->out * 1.5; waveform = 0; };
node w8   osc::simple { freq = freq->out; waveform = 0; };
node w4   osc::simple {
    freq = freq->out * 2 / (1 + clamp((freq->out * 2 - 5920) * 1000, 0, 1));
    waveform = 0;
};
node w223 osc::simple {
    freq = freq->out * 3 / (1 + clamp((freq->out * 3 - 5920) * 1000, 0, 1));
    waveform = 0;
};
node w2   osc::simple {
    freq = freq->out * 4 / (1 + clamp((freq->out * 4 - 5920) * 1000, 0, 1));
    waveform = 0;
};
node w135 osc::simple {
    freq = freq->out * 5 / (1 + clamp((freq->out * 5 - 5920) * 1000, 0, 1));
    waveform = 0;
};
node w113 osc::simple {
    freq = freq->out * 6 / (1 + clamp((freq->out * 6 - 5920) * 1000, 0, 1));
    waveform = 0;
};
node w1   osc::simple {
    freq = freq->out * 8 / (1 + clamp((freq->out * 8 - 5920) * 1000, 0, 1));
    waveform = 0;
};

# 3 dB a step, and 0 is off: 10^((bar - 8) * 3 / 20), gated by min(bar, 1).
node bars math::add {
    in0 = w16->out  * (pow(10, (@db16  - 8) * 0.15) * min(@db16,  1) + @leak) +
          w513->out * (pow(10, (@db513 - 8) * 0.15) * min(@db513, 1) + @leak) +
          w8->out   * (pow(10, (@db8   - 8) * 0.15) * min(@db8,   1) + @leak);
    in1 = w4->out   * (pow(10, (@db4   - 8) * 0.15) * min(@db4,   1) + @leak) +
          w223->out * (pow(10, (@db223 - 8) * 0.15) * min(@db223, 1) + @leak) +
          w2->out   * (pow(10, (@db2   - 8) * 0.15) * min(@db2,   1) + @leak) +
          w135->out * (pow(10, (@db135 - 8) * 0.15) * min(@db135, 1) + @leak) +
          w113->out * (pow(10, (@db113 - 8) * 0.15) * min(@db113, 1) + @leak) +
          w1->out   * (pow(10, (@db1   - 8) * 0.15) * min(@db1,   1) + @leak);
};

node percwheel osc::simple {
    freq = freq->out * @percharm;
    waveform = 0;
};

node percenv env::ad {
    a = 1 ms;
    d = @percdecay;
};

node noise osc::noise {
    color = 0;
};

node clicktone filt::svf {
    in = noise->out;
    cutoff = 3000;
    res = 0.2;
};

node clickenv env::ad {
    a = 0;
    d = 6 ms;
};

node scanner delay::chorus {
    in = bars->out * 0.2 + percwheel->out * percenv->out * @perc * 0.4 +
         clicktone->out_band * clickenv->out * @click * 0.5;
    rate = 6.9;
    depth = @scanner;
    delay = 2 ms;
    taps = 1;
    mix = @vchorus;
    phase = 0;
};

node env env::adsr {
    a = 2 ms;
    d = 0;
    s = 1;
    r = @r;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = scanner->out;
    in1 = env->out;
};

io ionode;
