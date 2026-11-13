# EQ -- four bands a side: a low shelf, two peaks and a high shelf.
#
# Each band is a filt::eq, so at 0 dB it passes the channel through bit for
# bit, and this graph at its defaults is a wire: putting it on a channel
# changes nothing until a gain is turned. Each band's freq, gain and q are
# knobs, and the bands overlap so that any one region has two of them to
# reach it with.
#
# A shelf's q above 0.707 steepens its corner and puts a bump either side
# of it, a peak's narrows it; see plugins/filt/eq.cpp. Nothing here limits
# the level, so a boost on a loud channel is over full scale, and
# fx/limiter.dsp on the master is where that is caught.

name "EQ";
author "Misha Nasledov";
description "A four-band parametric EQ: a low shelf, two peaks and a high shelf.";
category "Effects";

    @lowfreq = 100;
    @lowfreq.widget = 1;
    @lowfreq.min = 20;
    @lowfreq.max = 1000;
    @lowfreq.label = "Low Freq (Hz)";
    @lowgain = 0;
    @lowgain.widget = 1;
    @lowgain.min = -24;
    @lowgain.max = 24;
    @lowgain.label = "Low Gain (dB)";
    @lowq = 0.707;
    @lowq.widget = 1;
    @lowq.min = 0.1;
    @lowq.max = 2;
    @lowq.label = "Low Q";

    @lomidfreq = 400;
    @lomidfreq.widget = 1;
    @lomidfreq.min = 40;
    @lomidfreq.max = 4000;
    @lomidfreq.label = "Low Mid Freq (Hz)";
    @lomidgain = 0;
    @lomidgain.widget = 1;
    @lomidgain.min = -24;
    @lomidgain.max = 24;
    @lomidgain.label = "Low Mid Gain (dB)";
    @lomidq = 1;
    @lomidq.widget = 1;
    @lomidq.min = 0.1;
    @lomidq.max = 18;
    @lomidq.label = "Low Mid Q";

    @himidfreq = 2500;
    @himidfreq.widget = 1;
    @himidfreq.min = 200;
    @himidfreq.max = 16000;
    @himidfreq.label = "High Mid Freq (Hz)";
    @himidgain = 0;
    @himidgain.widget = 1;
    @himidgain.min = -24;
    @himidgain.max = 24;
    @himidgain.label = "High Mid Gain (dB)";
    @himidq = 1;
    @himidq.widget = 1;
    @himidq.min = 0.1;
    @himidq.max = 18;
    @himidq.label = "High Mid Q";

    @highfreq = 8000;
    @highfreq.widget = 1;
    @highfreq.min = 1000;
    @highfreq.max = 20000;
    @highfreq.label = "High Freq (Hz)";
    @highgain = 0;
    @highgain.widget = 1;
    @highgain.min = -24;
    @highgain.max = 24;
    @highgain.label = "High Gain (dB)";
    @highq = 0.707;
    @highq.widget = 1;
    @highq.min = 0.1;
    @highq.max = 2;
    @highq.label = "High Q";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = highl->out;
    out1 = highr->out;
};

node lowl filt::eq {
    in = ionode->in0;
    type = 1;
    freq = @lowfreq;
    gain = @lowgain;
    q = @lowq;
};

node lomidl filt::eq {
    in = lowl->out;
    type = 0;
    freq = @lomidfreq;
    gain = @lomidgain;
    q = @lomidq;
};

node himidl filt::eq {
    in = lomidl->out;
    type = 0;
    freq = @himidfreq;
    gain = @himidgain;
    q = @himidq;
};

node highl filt::eq {
    in = himidl->out;
    type = 2;
    freq = @highfreq;
    gain = @highgain;
    q = @highq;
};

node lowr filt::eq {
    in = ionode->in1;
    type = 1;
    freq = @lowfreq;
    gain = @lowgain;
    q = @lowq;
};

node lomidr filt::eq {
    in = lowr->out;
    type = 0;
    freq = @lomidfreq;
    gain = @lomidgain;
    q = @lomidq;
};

node himidr filt::eq {
    in = lomidr->out;
    type = 0;
    freq = @himidfreq;
    gain = @himidgain;
    q = @himidq;
};

node highr filt::eq {
    in = himidr->out;
    type = 2;
    freq = @highfreq;
    gain = @highgain;
    q = @highq;
};

io ionode;
