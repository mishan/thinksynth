name "test";

node ionode {
	out0 = filt->out_low;
	out1 = filt->out_low;
	channels = 2;
	play = 1;

	cutlfo = .025;
	reslfo = 0;

	mincut = 0.25;
	maxcut = 0.75;
	minres = 0.50;
	maxres = 0.99;

	pitch = 80;
	waveform = 1;
};

node cutlfo osc::simple {  # Tri-wave LFO for cutoff
	freq = ionode->cutlfo;
	waveform = 3;
};

node reslfo osc::simple {  # Tri-wave LFO for resonance
	freq = ionode->reslfo;
	waveform = 0;
};

node cutmap env::map {  # Map the cutoff LFO to proper values
	in = cutlfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->mincut;
	outmax = ionode->maxcut;
};

node resmap env::map {  # Map the resonance LFO to proper values
	in = reslfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->minres;
	outmax = ionode->maxres;
};

node osc osc::simple {  # The oscillator
	freq = ionode->pitch;
	waveform = ionode->waveform;
};

node filt filt::moog {  # Do the actual filtering
	in = osc->out;
	cutoff = cutmap->out;
	res = resmap->out;
};

io ionode;
