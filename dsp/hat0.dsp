name "test";

node ionode {
	channels = 2;
	out0 = mixer->out;
	out1 = mixer->out;
	play = adsr->play;

	freq = 940;
	freqmul = 3.14;
	depth1 = 0.1;
	depth2 = 0.25;

	open = 1;	# Sets envelope length  0 = closed, 1 = open
				# In between = in between

	attack = 2000;
	olen = 6000;
	clen = 2000;
	midp = 40;

	cutmin = 0.4;
	cutmax = 0.7;

	waveform = 2;
};

node decay mixer::fade {
	in0 = ionode->clen;
	in1 = ionode->olen;
	fade = ionode->open;
};

node adsr env::adsr {
	a = 0;
	d = ionode->attack;
	s = ionode->midp;
	r = decay->out;
	trigger = 0;
};

node static osc::static {
	stupid_bug = 1;
};

node freqmul math::mul {
	in0 = ionode->freq;
	in1 = ionode->freqmul;
};

node osc1 osc::simple {
	freq = ionode->freq;
	waveform = ionode->waveform;
	fm = static->out;
	fmamt = ionode->depth1;
};

node osc2 osc::simple {
	freq = freqmul->out;
	waveform = ionode->waveform;
	fm = static->out;
	fmamt = ionode->depth2;
};

node ringmod mixer::mul {
	in0 = osc1->out;
	in1 = osc2->out;
};

node filtmap env::map {
	in = adsr->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->cutmin;
	outmax = ionode->cutmax;
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