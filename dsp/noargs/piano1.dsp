# Piano-like synth
# Leif Ames <ink@bespin.org>
# 5-4-2004

name "test";

node ionode {

# standard thinksynth crap

    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = env->play;


# min and max of frequency for the band limited osc
    sfreqlo = 1000;
    sfreqhi = 7000;

# pulse width
    pw1 = 0.32;
    pw2 = 0.5;

# filter res
    res = 0.6;

# osc mixing and pitch offset (for osc2)
    oscmul = 2.0004;
    oscfade = 0.2;

# filter minimum and maximum
    fmin = 0.0;
    fmax = 0.6;

# amp envelope
    a = 2 ms;
    d = 200 ms;
    s = 0.6;    # 1 = full, 0 = off
    f = 10000 ms;
    r = 500 ms;

# filter envelope
    fa = 7 ms;
    fd = 1000 ms;
    fs = 30%;
    ff = 10000 ms;
    fr = 500 ms;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node suscalc math::mul {
    in0 = ionode->velocity;
    in1 = ionode->s;
};

node env env::adsfr {
    a = ionode->a;
    d = ionode->d;
    s = suscalc->out;
    f = ionode->f;
    r = ionode->r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node fenv env::adsfr {
    a = ionode->fa;
    d = ionode->fd;
    s = ionode->fs;
    f = ionode->ff;
    r = ionode->fr;
    trigger = ionode->trigger;
};

node freqmul math::mul {
    in0 = freq->out;
    in1 = ionode->oscmul;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = env->out;
};

node map1 env::map {  # filter map
    in = fenv->out;
    inmin = 0;
    inmax = th_max;
    outmin = ionode->fmin;
    outmax = ionode->fmax;
};

node map2 env::map {
    in = env->out;
    inmin = th_min;
    inmax = th_max;
    outmin = ionode->sfreqlo;
    outmax = ionode->sfreqhi;
};

node filt filt::ink {
    in = mixer2->out;
    cutoff = map1->out;
    res = ionode->res;
};

node osc osc::softsqr {
    freq = freq->out;
    sfreq = map2->out;
    pw = ionode->pw1;
};

node osc2 osc::softsqr {
    freq = freqmul->out;
    sfreq = map2->out;
    pw = ionode->pw2;
};

node mixer2 mixer::fade {
    in0 = osc->out;
    in1 = osc2->out;
    fade = ionode->oscfade;
};

io ionode;
