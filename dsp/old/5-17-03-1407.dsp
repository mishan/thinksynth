# $Id: 5-17-03-1407.dsp,v 1.1 2004/09/15 06:07:43 ink Exp $
name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;

	waveform = 1;
	pw = 0.3;
	subosc = 0.4;
	res = 0.7;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node lowfreq math::div {
	in0 = freq->out;
	in1 = 2;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::adsr {
	a = 0;
	d = 180000;
	s = 100;
	r = 10000;
	trigger = 0;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = 0;
	outmax = 1;
};

node filt filt::ink {
	in = mixer2->out;
	cutoff = map1->out;
	res = ionode->res;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
	pw = ionode->pw;
};

node osc2 osc::simple {
	freq = lowfreq->out;
	waveform = 2;
	pw = 0.1;
};

node mixer2 mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->subosc;
};

io ionode;
