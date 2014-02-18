# TS-1
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "Brass Box";
author "Leif Ames";
description "Made for brass";


    @buzz = 80ms;
    @buzz.min = 0;
    @buzz.max = 2000ms;
    @buzz.widget = 1;
    @buzz.label = "Buzz Length";
    @buzzf = 28;
    @buzzf.min = 0;
    @buzzf.max = 50;
    @buzzf.widget = 1;
    @buzzf.label = "Buzz Frequency";    

    @bend = -1;
    @bend.min = -24;
    @bend.max = 24;
    @bend.widget = 1;
    @bend.label = "Bend Amount";    
    @bendl = 18ms;
    @bendl.min = 0;
    @bendl.max = 2000ms;
    @bendl.widget = 1;
    @bendl.label = "Bend Length";

    @pw1 = 0.2;
    @pw1.widget = 1;
    @pw1.min = 0;
    @pw1.max = 1;
    @pw1.label = "Pulse Width 1";
    @pw2 = 0.1;
    @pw2.widget = 1;
    @pw2.min = 0;
    @pw2.max = 1;
    @pw2.label = "Pulse Width 2";

    @res = 2;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 15;
    @res.label = "Resonance";
    @cutoff = 4;
    @cutoff.widget = 1;
    @cutoff.min = 0;
    @cutoff.max = 16;
    @cutoff.label = "Cutoff";
    @famt = 2;
    @famt.widget = 1;
    @famt.min = 0;
    @famt.max = 16;
    @famt.label = "Filter Envelope";

    @oscmul = 2;
    @oscmul.widget = 1;
    @oscmul.min = 0;
    @oscmul.max = 9;
    @oscmul.label = "Osc2 Multiplier";
    @oscfade = 0.25;
    @oscfade.widget = 1;
    @oscfade.min = 0;
    @oscfade.max = 1;
    @oscfade.label = "Osc Fade";

    @a = 8 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 2000ms;
    @a.label = "Attack";
    @d = 60 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 2000ms;
    @d.label = "Decay";
    @s = 0.6;    # 1 = full, 0 = off
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";
    @r = 10 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 2000ms;
    @r.label = "Release";


node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = env->play;
};

# pitch = ionode->note + (@bend * bend envelope)

node bendenv env::adsr {
    a = 1;
    d = @bendl;
    s = 0;
    r = 1;
    trigger = 0;
};

node bendcalc1 math::mul {
    in0 = @bend;
    in1 = bendenv->out;
};

node bendcalc2 math::add {
    in0 = bendcalc1->out;
    in1 = ionode->note;
};

node freq misc::midi2freq {
    note = bendcalc2->out;
};

node cutcalc math::mul {
    in0 = freq->out;
    in1 = @cutoff;
};

node cutcalc2 math::mul {    # multiply the filter start by famt
    in0 = cutcalc->out;
    in1 = @famt;
};

node suscalc math::mul {
    in0 = ionode->velocity;
    in1 = @s;
};

node buzzenv env::adsr {
    a = 1;
    d = @buzz;
    s = 0;
    r = 1;
    p = 1;
    trigger = 0;
};

node buzzlfo osc::simple {
    freq = @buzzf;
    waveform = 3;
};

node buzzmap env::map {
    in = buzzlfo->out;
    inmin = th_min;
    inmax = th_max;
    outmin = 0;
    outmax = 1;
};

node buzzfade mixer::fade {
    in0 = 1;
    in1 = buzzmap->out;
    fade = buzzenv->out;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = suscalc->out;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node freqmul math::mul {
    in0 = freq->out;
    in1 = @oscmul;
};

node mixer mixer::mul {
    in0 = buzzmix->out;
    in1 = env->out;
};

node buzzmix math::mul {
    in0 = filt->out;
    in1 = buzzfade->out;
};

node map1 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = cutcalc->out;
    outmax = cutcalc2->out;
};

node osc osc::simple {
    freq = freq->out;
    waveform = 1;
    pw = @pw1;
};

node osc2 osc::simple {
    freq = freqmul->out;
    waveform = 1;
    pw = @pw2;
};

node mixer2 mixer::fade {
    in0 = osc->out;
    in1 = osc2->out;
    fade = @oscfade;
};

node filt filt::res2pole2 {
    in = mixer2->out;
    cutoff = map1->out;
    res = @res;
};

io ionode;
