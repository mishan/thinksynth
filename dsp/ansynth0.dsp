node args misc::const {
	note = 7;
	octave = 1;
	length = 10000;
	filtlo = 400;
	filthi = 100;
	waveform = 2;
	subwave = 2;
	fade = 0.6;
	pwhigh = 0.5;
	pwlow = 0.1;
};
node ntf1 misc::note2freq {
	note = args->$note;
	octave = args->$octave;
};
node osc1 osc::simple {
	freq = ntf1->$freq;
	waveform = args->$waveform;
	pw = map2->out;
};
node div1 math::div {
	in0 = ntf1->$freq;
	in1 = 2;
};
node subosc1 osc::simple {
	freq = div1->out;
	waveform = args->$subwave;
	pw = map2->out;
};
node mix1 mixer::fade {
	in0 = osc1->out;
	in1 = subosc1->out;
	fade = args->$fade;
};
node adsr1 env::adsr {
	al = 2500;
	dl = 3500;
	sl = args->$length; 
	rl = 10000;
	l1 = 100;
	l2 = 40;
	l3 = 15;
	trigger = 1;
};
node adsr2 env::adsr {
	al = 3000;
	dl = 3000;
	sl = args->$length; 
	rl = 10000;
	l1 = 100;
	l2 = 60;
	l3 = 30;
	trigger = 1;
};
node map1 env::map {
	in = adsr2->out;
	inmin = 0;
	inmax = 100;
	outmin = args->$filtlo;
	outmax = args->$filthi;
};
node map2 env::map {
	in = adsr2->out;
	inmin = 0;
	inmax = 100;
	outmin = args->$pwlow;
	outmax = args->$pwhigh;
};
node filt1 filt::ink {
	in = mix1->out;
	factor = map1->out;
	grav = 0.01;
	frict = 0.07;
	limit = 80;
};
node am1 env::am {
	in = filt1->out;
	conin = adsr1->out;
	conmax = 100;
};
node stat misc::const {
	play = adsr1->play;
	left = am1->out;
	right = am1->out;
};
io stat;
