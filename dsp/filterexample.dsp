name "test";

node ionode {
	out0 = filt->out;
	out1 = filt->out;
	channels = 2;
	play = 1;

	cutlfo = 0.5;
	reslfo = 0.03;

	mincut = 0.01;
	maxcut = 0.99;
	minres = 0.01;
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
	waveform = 3;
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

node filt filt::res1pole {  # Do the actual filtering
	in = osc->out;
	cutoff = cutmap->out;
	res = resmap->out;
};

io ionode;
