# $Id: bd1.dsp,v 1.1 2004/12/07 02:38:57 ink Exp $
name "bd";

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
	s = 100;
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
