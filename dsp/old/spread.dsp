# $Id: spread.dsp,v 1.1 2004/09/15 06:07:43 ink Exp $
name "test";

node ionode {
	channels = 2;
	out0 = mixer3->out;
	out1 = mixer3->out;
	play = 1;

	amp = 2;

	wmin = 0.4;
	wmax = 0.6;
	wdiv = 2;
	fadd = 4;

	pw = 0.5;

	fade1 = 0.4;	# Osc 1 and 2
	fade2 = 0.4;	# Osc 3 and 4
	fade3 = 0.4;	# Fade 1 and 2
};

node freq misc::midi2freq {
	note = ionode->note;
};

node wdiv math::div {
	in0 = freq->out;
	in1 = ionode->wdiv;
};

node wlfo osc::simple {
	freq = wdiv->out;
	waveform = 0;
};

node wmap env::map {
	in = wlfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->wmin;
	outmax = ionode->wmax;
};

node freqsin1 math::sin {
	index = 1;
	wavelength = wmap->out;
	amp = ionode->amp;
};

node freqsin2 math::sin {
	index = 2;
	wavelength = wmap->out;
	amp = ionode->amp;
};

node freqsin3 math::sin {
	index = 3;
	wavelength = wmap->out;
	amp = ionode->amp;
};

node freqsin4 math::sin {
	index = 4;
	wavelength = wmap->out;
	amp = ionode->amp;
};

node freqadd1 math::add {
	in0 = ionode->fadd;
	in1 = freqsin1->out;
};

node freqadd2 math::add {
	in0 = ionode->fadd;
	in1 = freqsin2->out;
};

node freqadd3 math::add {
	in0 = ionode->fadd;
	in1 = freqsin3->out;
};

node freqadd4 math::add {
	in0 = ionode->fadd;
	in1 = freqsin4->out;
};

node freqmul1 math::mul {
	in0 = freq->out;
	in1 = freqadd1->out;
};

node freqmul2 math::mul {
	in0 = freq->out;
	in1 = freqadd2->out;
};

node freqmul3 math::mul {
	in0 = freq->out;
	in1 = freqadd3->out;
};

node freqmul4 math::mul {
	in0 = freq->out;
	in1 = freqadd4->out;
};

node osc1 osc::softsqr {
	freq = freq->out;
	sfreq = freqmul1->out;
	pw = ionode->pw;
};

node osc2 osc::softsqr {
	freq = freq->out;
	sfreq = freqmul2->out;
	pw = ionode->pw;
};

node osc3 osc::bandosc {
	freq = freq->out;
	band = freqmul3->out;
	pw = ionode->pw;
};

node osc4 osc::bandosc {
	freq = freq->out;
	band = freqmul4->out;
	pw = ionode->pw;
};

node mixer1 mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->fade1;
};

node mixer2 mixer::fade {
	in0 = osc3->out;
	in1 = osc4->out;
	fade = ionode->fade2;
};

node mixer3 mixer::fade {
	in0 = mixer1->out;
	in1 = mixer2->out;
	fade = ionode->fade3;
};


io ionode;