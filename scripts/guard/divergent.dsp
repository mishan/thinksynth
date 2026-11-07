# divergent.dsp -- a graph whose filter runs away, on purpose.
#
# Beside the harness rather than in dsp/, for the reason the other two
# fixtures here give: it is not an instrument and every corpus sweep globs
# dsp/. Unlike nonfinite.dsp it is not arithmetic that cannot be done -- it is
# an ordinary filter given an ordinary-looking number, which is what makes it
# worth keeping. `mixer.out' reaches -inf within seven windows.
#
# THE NUMBER IS `s = 100'. env::adsr's sustain is a level, 0 to 1 full scale,
# and 100 is a hundred times full scale. The envelope therefore hands
# filt::divbuf a `factor' a hundred times larger than anything the plugin was
# written for, and divbuf divides by it; the feedback that results grows every
# sample. The file dates from when TH_MAX was an integer full scale and `100'
# meant what `100%' means now, which is why it looks like a typo and is really
# a unit that moved.
#
# This is dsp/noargs/bd1.dsp, 2004, kept for what it breaks rather than for
# what it sounds like. Cited by:
#
#   plugins/filt/divbuf.cpp      the factor an envelope can hand it
#   plugins/visual/meter.cpp     a meter that must survive -inf
#   scripts/visualcheck.cpp      where its -inf and NaN feeds come from
#   scripts/dspprobe.cpp         a probe on a port that has gone non-finite
#
# and it is the only user of filt::divbuf anywhere in the tree, so deleting it
# would leave that plugin with no coverage at all. Do not "fix" the sustain:
# the divergence is the point, and every one of those four files is claiming
# that this behaviour is real rather than invented.

name "Guard: divergent";
description "A filter driven by an envelope a hundred times full scale. Runs away by design.";

node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = env->play;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = env->out;
};

node env env::adsr {
    a = 0;
    d = 4000;
    s = 100;        # a hundred times full scale -- see the head
    r = 4000;
    trigger = 0;
};

node map1 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = 30;
    outmax = 90;
};

node map2 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = 0;
    outmax = 1;
};

node map3 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = 100;
    outmax = 1000;
};

node filt filt::divbuf {
    in = osc->out;
    factor = map2->out;
};

node osc osc::softsqr {
    freq = map1->out;
    sfreq = map3->out;
    pw = 0.3;
};

io ionode;
