name "test";

node ionode {
	out0 = envmix->out;
	out1 = envmix->out;
	channels = 2;
	play = adsr->play;

	a = 0.5ms;
	d = 0.5ms;
	s = 90%;
	r = 5ms;
	

	inwav = 5;
	orgmul1 = 1;
	orgmul2 = 2;
	orgmul3 = 4;
	orgmul4 = 8;
	orgmul5 = 16;
	orgmul6 = 3;
	orgmul7 = 5;
	orgmul8 = 7;
	fade12 = 0.3;	# the lower two
	fade34 = 0.4;	# the upper two
	fade1234 = 0.35;	# fade12 and fade34
	fade56 = antivel->out;		# the lower two
	fade78 = ionode->velocity;	# the upper two
	fade5678 = antivel->out;	# fade56 and fade78
	fade12345678 = 0.3;	# the whole shebang
};

node freq misc::midi2freq {
	note = ionode->note;
};

node antivel math::sub {
	in0 = 1;
	in1 = ionode->velocity;
};

node input2a osc::simple {	# small drawbar organ input section
	freq = freq->out;
	waveform = ionode->inwave;
	mul = ionode->orgmul1;
};

node input2b osc::simple {
	freq = freq->out;
	waveform = ionode->inwave;
	mul = ionode->orgmul2;
};

node input2c osc::simple {
	freq = freq->out;
	waveform = ionode->inwave;
	mul = ionode->orgmul3;
};

node input2d osc::simple {
	freq = freq->out;
	waveform = ionode->inwave;
	mul = ionode->orgmul4;
};

node input2e osc::simple {	# small drawbar organ input section
	freq = freq->out;
	waveform = ionode->inwave;
	mul = ionode->orgmul5;
};

node input2f osc::simple {
	freq = freq->out;
	waveform = ionode->inwave;
	mul = ionode->orgmul6;
};

node input2g osc::simple {
	freq = freq->out;
	waveform = ionode->inwave;
	mul = ionode->orgmul7;
};

node input2h osc::simple {
	freq = freq->out;
	waveform = ionode->inwave;
	mul = ionode->orgmul8;
};

node fade12 mixer::fade {
	in0 = input2a->out;
	in1 = input2b->out;
	fade = ionode->fade12;
};

node fade34 mixer::fade {
	in0 = input2c->out;
	in1 = input2d->out;
	fade = ionode->fade34;
};

node fade1234 mixer::fade {
	in0 = fade12->out;
	in1 = fade34->out;
	fade = ionode->fade1234;
};

node fade56 mixer::fade {
	in0 = input2e->out;
	in1 = input2f->out;
	fade = ionode->fade56;
};

node fade78 mixer::fade {
	in0 = input2g->out;
	in1 = input2h->out;
	fade = ionode->fade78;
};

node fade5678 mixer::fade {
	in0 = fade56->out;
	in1 = fade78->out;
	fade = ionode->fade5678;
};

node fade12345678 mixer::fade {
	in0 = fade1234->out;
	in1 = fade5678->out;
	fade = ionode->fade12345678;
};

node adsr env::adsr {
	trigger = ionode->trigger;
	a = ionode->a;
	d = ionode->d;
	s = ionode->s;
	r = ionode->r;
};

node envmix mixer::mul {
	in0 = fade12345678->out;
	in1 = adsr->out;
};

io ionode;