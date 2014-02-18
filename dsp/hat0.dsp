name "Hat 0";
author "Leif Ames";
description "Electronic Hihat";

    @freq = 940;
    @freq.widget = 1;
    @freq.min = 0;
    @freq.max = 3000;
    @freq.label = "Frequency";
    @freqmul = 3.141;
    @freqmul.widget = 1;
    @freqmul.min = 0;
    @freqmul.max = 8;
    @freqmul.label = "Frequency Multiply";
    @depth1 = 0.1;
    @depth1.widget = 1;
    @depth1.min = 0;
    @depth1.max = 1;
    @depth1.label = "Depth 1";
    @depth2 = 0.25;
    @depth2.widget = 1;
    @depth2.min = 0;
    @depth2.max = 1;
    @depth2.label = "Depth 2";

    @attack = 10 ms;
    @attack.widget = 1;
    @attack.min = 0;
    @attack.max = 500ms;
    @attack.label = "Attack";

    @olen = 1000 ms;
    @olen.widget = 1;
    @olen.min = 0;
    @olen.max = 4000ms;
    @olen.label = "Open Length";

    @clen = 200 ms;
    @clen.widget = 1;
    @clen.min = 0;
    @clen.max = 4000ms;
    @clen.label = "Closed Length";

    @midp = 0.2;
    @midp.widget = 1;
    @midp.min = 0;
    @midp.max = 1;
    @midp.label = "Envelope Midpoint";

    @cutmin = 0.4;
    @cutmin.widget = 1;
    @cutmin.min = 0;
    @cutmin.max = 1;
    @cutmin.label = "Filter Low";
    @cutmax = 0.7;
    @cutmax.widget = 1;
    @cutmax.min = 0;
    @cutmax.max = 1;
    @cutmax.label = "Filter High";

node ionode {
    channels = 2;
    out0 = mixer->out;
    out1 = mixer->out;
    play = adsr->play;

    waveform = 2;
};

node vcurve math::mul {        # velocity curve
    in0 = ionode->velocity;
    in1 = ionode->velocity;
};

node decay mixer::fade {
    in0 = @clen;
    in1 = @olen;
    fade = vcurve->out;
};

node adsr env::adsr {
    a = 0;
    d = @attack;
    s = @midp;
    r = decay->out;
    trigger = 0;
};

node static osc::static {
};

node freqmul math::mul {
    in0 = @freq;
    in1 = @freqmul;
};

node osc1 osc::simple {
    freq = @freq;
    waveform = ionode->waveform;
    fm = static->out;
    fmamt = @depth1;
};

node osc2 osc::simple {
    freq = freqmul->out;
    waveform = ionode->waveform;
    fm = static->out;
    fmamt = @depth2;
};

node ringmod mixer::mul {
    in0 = osc1->out;
    in1 = osc2->out;
};

node filtmap env::map {
    in = adsr->out;
    inmin = 0;
    inmax = th_max;
    outmin = @cutmin;
    outmax = @cutmax;
};

node filter filt::ds {
    in = ringmod->out;
    cutoff = filtmap->out;
};

node mixer mixer::mul {
    in0 = filter->out_high;
    in1 = adsr->out;
};

io ionode;