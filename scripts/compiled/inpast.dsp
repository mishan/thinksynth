# dspcompile.inpast's: an effect of one channel reading a second input,
# which the engine never writes, so dspcompile refuses it, though the
# effect has a second output.
name "Inpast";
category "Experiments";

node ionode {
    channels = 1;
    in0 = 0;
    in1 = 0;
    out0 = m->out;
    out1 = m->out;
};

node m math::mul { in0 = ionode->in1; in1 = 0.5; };

io ionode;
