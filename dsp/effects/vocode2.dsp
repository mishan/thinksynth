name "test";

node ionode {
	out0 = outadd8->out;
	out1 = outadd8->out;
	channels = 2;
	play = input1->play;

	f1 = 50;
	f2 = 100;
	f3 = 200;
	f4 = 400;
	f5 = 800;
	f6 = 1600;
	f7 = 3200;
	f8 = 6400;

	res = 12;

	fampmin = 0;
	fampmax = 0.3;
	falloff = 2.2;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node input1 input::wav {
};

node input2 osc::simple {
	freq = freq->out;
	waveform = 5;
	fm = inputfm->out;
	fmamt = 2;
};

node inputfm osc::simple {
	freq = freq->out;
	waveform = 0;
	mul = 2;
};

node filt1 filt::res2pole {
	in = input1->out;
	cutoff = ionode->f1;
	res = ionode->res;
};

node follower1 env::followavg {
	in = filt1->out;
	falloff = ionode->falloff;
};

node filteramp1 env::map {
	in = follower1->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fampmin;
	outmax = ionode->fampmax;
};

node filtout1 filt::res2pole {
	in = input2->out;
	cutoff = ionode->f1;
	res = ionode->res;
};

node fmul1 math::mul {
	in0 = filtout1->out;
	in1 = filteramp1->out;
};


node filt2 filt::res2pole {
	in = input1->out;
	cutoff = ionode->f2;
	res = ionode->res;
};

node follower2 env::followavg {
	in = filt2->out_band;
	falloff = ionode->falloff;
};

node filteramp2 env::map {
	in = follower2->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fampmin;
	outmax = ionode->fampmax;
};

node filtout2 filt::res2pole {
	in = filtout1->out_high;
	cutoff = ionode->f2;
	res = ionode->res;
};

node fmul2 math::mul {
	in0 = filtout2->out;
	in1 = filteramp2->out;
};

node outadd2 math::add {
	in0 = fmul1->out;
	in1 = fmul2->out;
};



node filt3 filt::res2pole {
	in = input1->out;
	cutoff = ionode->f3;
	res = ionode->res;
};

node follower3 env::followavg {
	in = filt3->out_band;
	falloff = ionode->falloff;
};

node filteramp3 env::map {
	in = follower3->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fampmin;
	outmax = ionode->fampmax;
};

node filtout3 filt::res2pole {
	in = filtout2->out_high;
	cutoff = ionode->f3;
	res = ionode->res;
};

node fmul3 math::mul {
	in0 = filtout3->out;
	in1 = filteramp3->out;
};

node outadd3 math::add {
	in0 = outadd2->out;
	in1 = fmul3->out;
};



node filt4 filt::res2pole {
	in = input1->out;
	cutoff = ionode->f4;
	res = ionode->res;
};

node follower4 env::followavg {
	in = filt4->out_band;
	falloff = ionode->falloff;
};

node filteramp4 env::map {
	in = follower4->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fampmin;
	outmax = ionode->fampmax;
};

node filtout4 filt::res2pole {
	in = filtout3->out_high;
	cutoff = ionode->f4;
	res = ionode->res;
};

node fmul4 math::mul {
	in0 = filtout4->out;
	in1 = filteramp4->out;
};

node outadd4 math::add {
	in0 = outadd3->out;
	in1 = fmul4->out;
};



node filt5 filt::res2pole {
	in = input1->out;
	cutoff = ionode->f5;
	res = ionode->res;
};

node follower5 env::followavg {
	in = filt5->out_band;
	falloff = ionode->falloff;
};

node filteramp5 env::map {
	in = follower5->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fampmin;
	outmax = ionode->fampmax;
};

node filtout5 filt::res2pole {
	in = filtout4->out_high;
	cutoff = ionode->f5;
	res = ionode->res;
};

node fmul5 math::mul {
	in0 = filtout5->out;
	in1 = filteramp5->out;
};

node outadd5 math::add {
	in0 = outadd4->out;
	in1 = fmul5->out;
};



node filt6 filt::res2pole {
	in = input1->out;
	cutoff = ionode->f6;
	res = ionode->res;
};

node follower6 env::followavg {
	in = filt6->out_band;
	falloff = ionode->falloff;
};

node filteramp6 env::map {
	in = follower6->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fampmin;
	outmax = ionode->fampmax;
};

node filtout6 filt::res2pole {
	in = filtout5->out_high;
	cutoff = ionode->f6;
	res = ionode->res;
};

node fmul6 math::mul {
	in0 = filtout6->out;
	in1 = filteramp6->out;
};

node outadd6 math::add {
	in0 = outadd5->out;
	in1 = fmul6->out;
};



node filt7 filt::res2pole {
	in = input1->out;
	cutoff = ionode->f7;
	res = ionode->res;
};

node follower7 env::followavg {
	in = filt7->out_band;
	falloff = ionode->falloff;
};

node filteramp7 env::map {
	in = follower7->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fampmin;
	outmax = ionode->fampmax;
};

node filtout7 filt::res2pole {
	in = filtout6->out_high;
	cutoff = ionode->f7;
	res = ionode->res;
};

node fmul7 math::mul {
	in0 = filtout7->out;
	in1 = filteramp7->out;
};

node outadd7 math::add {
	in0 = outadd6->out;
	in1 = fmul7->out;
};



node filt8 filt::res2pole {
	in = input1->out;
	cutoff = ionode->f8;
	res = ionode->res;
};

node follower8 env::followavg {
	in = filt8->out_high;
	falloff = ionode->falloff;
};

node filteramp8 env::map {
	in = follower8->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fampmin;
	outmax = ionode->fampmax;
};

node filtout8 filt::res2pole {
	in = filtout7->out_high;
	cutoff = ionode->f8;
	res = ionode->res;
};

node fmul8 math::mul {
	in0 = filtout8->out_high;
	in1 = filteramp8->out;
};

node outadd8 math::add {
	in0 = outadd7->out;
	in1 = fmul8->out;
};


io ionode;