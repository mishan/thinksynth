name "test";

node ionode {
	out0 = filt->out;
	out1 = filt->out;
	channels = 2;
	play = 1;

	cutlfo = 1;
	reslfo = 0.1;

	mincut = 0;
	maxcut = 1;
	minres = 0;
	maxres = 0.95;

	pitch = 55;
	waveform = 1;
	pw = 0.4;
};

node cutlfo osc::simple {  # Tri-wave LFO for cutoff
	freq = ionode->cutlfo;
	waveform = 0;
	pw = 0.5;
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
	pw = ionode->pw;
};

node filt filt::ink {  # Do the actual filtering
	in = osc->out;
	cutoff = cutmap->out;
	res = resmap->out;
};

io ionode;
