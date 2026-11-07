# A sine through a gain in dB: the smallest instrument with a control in
# a unit the language folds.
name "Gain";
    @gain = -6 dB;
    @gain.widget = 1;
    @gain.min = -40 dB;
    @gain.max = 0 dB;
    @gain.label = "Gain";

node ionode { channels = 1; out0 = out->out; play = env->play; };
node env env::ad { a = 1 ms; d = 300 ms; };
node tone osc::simple { freq = 220; waveform = 0; };
node out math::mul { in0 = tone->out * env->out; in1 = @gain; };
io ionode;
