# $Id: 5-17-03-0540.dsp,v 1.1 2004/09/15 06:07:43 ink Exp $
name "test";

node ionode {
	out0 = filt->out;
	out1 = filt->out;
	channels = 2;
	play = 1;

	cutlfo = 0.0435;
	reslfo = 0.217;
	vibratolfo = 7;

	mincut = 0.1;
	maxcut = 1;
	minres = 0.1;
	maxres = 0.95;
	vibratoamt = 0.05;
	sqrsfreq = 1000;
	sqrpw = 0.2;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node cutlfo osc::simple {
	freq = ionode->cutlfo;
	waveform = 3;
};

node reslfo osc::simple {
	freq = ionode->reslfo;
	waveform = 3;
};

node vibratolfo osc::simple {
	freq = ionode->vibratolfo;
	waveform = 4;
};

node pwlfo osc::simple {
	freq = ionode->pwlfo;
	waveform = 0;
};

node cutmap env::map {
	in = cutlfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->mincut;
	outmax = ionode->maxcut;
};

node resmap env::map {
	in = reslfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->minres;
	outmax = ionode->maxres;
};

node vibratomul math::mul {
	in0 = vibratolfo->out;
	in1 = ionode->vibratoamt;  # How much does the vibrato bend it?
};

node vibratomix math::add {
	in0 = vibratomul->out;
	in1 = freq->out;
#	temp = vibratoprint->foo;
};

node vibratoprint misc::print {
	in = vibratomul->out;
};

node pwmap env::map {
	in = pwlfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->pwmin;
	outmax = ionode->pwmax;
};

node filt filt::ink {
	in = osc->out;
	cutoff = cutmap->out;
	res = resmap->out;
};

node osc osc::softsqr{
	freq = vibratomix->out;
	sfreq = ionode->sqrsfreq;
	pw = ionode->sqrpw;
};

io ionode;
