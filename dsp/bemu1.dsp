# $Id: bemu1.dsp,v 1.2 2004/02/09 10:50:28 misha Exp $
name "test";

node ionode {
	channels = 2;
	out0 = ampmix->out;
	out1 = ampmix->out;
	play = ampenv->play;

	filt_a = 7000;
	filt_d = 5000;
	filt_s = 80;
	filt_r = 1000000;

	amp_a = 0;
	amp_d = 4000;
	amp_s = 80;
	amp_r = 80000;

	cutofflo = 0.1;
	cutoffhi = 0.9;
	res = 0.8;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node filtenv env::adsr {
	a = ionode->filt_a;
	d = ionode->filt_d;
	s = ionode->filt_s;
	r = ionode->filt_r;
	trigger = 0;
};

node filtmap env::map {
	in = filtenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->cutofflo;
	outmax = ionode->cutoffhi;
};

node ampenv env::adsr {
	a = ionode->amp_a;
	d = ionode->amp_d;
	s = ionode->amp_s;
	r = ionode->amp_r;
	trigger = 0;
};

node ampmix mixer::mul {
	in0 = filt->out;
	in1 = ampenv->out;
};

node osc osc::simple {
	freq = freq->out;
	waveform = 1;
	reset = modosc->sync;
};

node modosc osc::simple {
	freq = freq->out;
	waveform = 2;
	mul = 2;	# Octave down
};

node modmap env::map {
	in = modosc->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 1;
	outmax = 0;
};

node ammod math::mul {
	in0 = osc->out;
	in1 = modmap->out;
};

node filt filt::ink {
	in = ammod->out;
	cutoff = filtmap->out;
	res = ionode->res;
};

io ionode;
