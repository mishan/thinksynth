# Piano-like synth
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "Resonant Piano";
author "Leif Ames";
description "Band-limited waveforms and some filtering";


    @sfreqlo = 1600;
    @sfreqlo.widget = 1;
    @sfreqlo.min = 100;
    @sfreqlo.max = 10000;
    @sfreqlo.label = "Band Low";
    @sfreqhi = 5500;
    @sfreqhi.widget = 1;
    @sfreqhi.min = 100;
    @sfreqhi.max = 10000;
    @sfreqhi.label = "Band High";

    @pw1 = 0.38;
    @pw1.widget = 1;
    @pw1.min = 0;
    @pw1.max = 1;
    @pw1.label = "Pulse Width 1";
    @pw2 = 0.75;
    @pw2.widget = 1;
    @pw2.min = 0;
    @pw2.max = 1;
    @pw2.label = "Pulse Width 2";

    @res = 0.4;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 1;
    @res.label = "Resonance";
    @fmin = 0;
    @fmin.widget = 1;
    @fmin.min = 0;
    @fmin.max = 1;
    @fmin.label = "Cutoff Low";
    @fmax = 0.9;
    @fmax.widget = 1;
    @fmax.min = 0;
    @fmax.max = 1;
    @fmax.label = "Cutoff High";
    @shape = 0.5;
    @shape.widget = 1;
    @shape.min = 0;
    @shape.max = 5;
    @shape.label = "Shape";

    @oscmul = 1.0001;
    @oscmul.widget = 1;
    @oscmul.min = 0;
    @oscmul.max = 9;
    @oscmul.label = "Osc2 Multiplier";
    @oscfade = 0.25;
    @oscfade.widget = 1;
    @oscfade.min = 0;
    @oscfade.max = 1;
    @oscfade.label = "Osc Fade";

    @a = 0.7 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 2000ms;
    @a.label = "Attack";
    @d = 50 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 2000ms;
    @d.label = "Decay";
    @s = 0.420;    # 1 = full, 0 = off
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";
    @f = 7000 ms;
    @f.widget = 1;
    @f.min = 0;
    @f.max = 20000ms;
    @f.label = "Falloff";
    @r = 400 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 5000ms;
    @r.label = "Release";


node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = env->play;

    #sfreqlo = 1600;
    #sfreqhi = 5500;

    #pw1 = 0.38;
    #pw2 = 0.75;

    #res = 5;
    #fmin = 0;
    #fmax = 8000;

    #oscmul = 1.0001;
    #oscfade = 0.25;

    #a = 0.7 ms;
    #d = 50 ms;
    #s = 0.420;    # 1 = full, 0 = off
    #f = 7000 ms;
    #r = 400 ms;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node suscalc math::mul {
    in0 = ionode->velocity;
    in1 = @s;
};

node env env::adsfr {
    a = @a;
    d = @d;
    s = suscalc->out;
    f = @f;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node freqmul math::mul {
    in0 = freq->out;
    in1 = @oscmul;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = env->out;
};

node map1 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = @fmin;
    outmax = @fmax;
};

node map2 env::map {
    in = env->out;
    inmin = th_min;
    inmax = th_max;
    outmin = @sfreqlo;
    outmax = @sfreqhi;
};

node filt filt::ink3 {
    in = mixer2->out;
    cutoff = map1->out;
    res = @res;
    shape = @shape;
};

node osc osc::softsqr {
    freq = freq->out;
    sfreq = map2->out;
    pw = @pw1;
};

node osc2 osc::softsqr {
    freq = freqmul->out;
    sfreq = map2->out;
    pw = @pw2;
};

node mixer2 mixer::fade {
    in0 = osc->out;
    in1 = osc2->out;
    fade = @oscfade;
};

io ionode;
