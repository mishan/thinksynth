# $Id: sd1.dsp,v 1.3 2004/02/09 10:50:28 misha Exp $
name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = snare_env->play;

	tonemix = 0.1;

	snare_len_d = 2000;
	snare_len_s = 40;
	snare_len_r = 3000;
	snare_freqhi = 1000;
	snare_freqlo = 800;
	snare_filtq = 0.5;

	tone_len_d = 1000;
	tone_len_s = 100;
	tone_len_r = 2000;
	tone_freqhi = 330;
	tone_freqlo = 220;
};

node mixer mixer::fade {
	in0 = snare_mix->out;
	in1 = tone_mix->out;
	fade = ionode->tonemix;
};

node snare_env env::adsr {
	a = 0;
	d = ionode->snare_len_d;
	s = ionode->snare_len_s;
	r = ionode->snare_len_r;
	trigger = 0;
};

node snare_freqmap env::map {
	in = snare_env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->snare_freqlo;
	outmax = ionode->snare_freqhi;
};

node snare_osc osc::static {
	stupid_bug = 0;
};

node snare_filt filt::res2pole {
	in = snare_osc->out;
	cutoff = snare_freqmap->out;
	res = ionode->snare_filtq;
};

node snare_mix mixer::mul {
	in0 = snare_filt->out_band;
	in1 = snare_env->out;
};

node tone_env env::adsr {
	a = 0;
	d = ionode->tone_len_d;
	s = ionode->tone_len_s;
	r = ionode->tone_len_r;
	trigger = 0;
};

node tone_freqmap env::map {
	in = tone_env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->tone_freqlo;
	outmax = ionode->tone_freqhi;
	trigger = 0;
};

node tone_osc osc::simple {
	freq = tone_freqmap->out;
	waveform = 5;
	pw = ionode->tone_pw;
};

node tone_mix mixer::mul {
	in0 = tone_osc->out;
	in1 = tone_env->out;
};
io ionode;
