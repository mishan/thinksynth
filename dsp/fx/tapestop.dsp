# Tape Stop -- the motor switched off under a channel, and back on.
#
# delay::varispeed plays what comes in back at a speed: at 1 a wire, and
# below it everything slows and drops in pitch together, the way a tape
# does when the motor stops. `Stop' at 1 winds the speed down to nothing
# in a straight line over `Time'; back at 0 the motor is running again at
# once, and the output crossfades to now, so a stop leaves nothing late
# behind it. A chain riding `fx.stop' -- up at the end of a bar, down at
# the next downbeat -- is the record stopping and the band coming back
# in.

name "Tape Stop";
author "Misha Nasledov";
description "A channel slowed to a halt and spun back up, pitch and all, by one knob.";
category "Effects";

    @stop = 0;
    @stop.widget = 1;
    @stop.min = 0;
    @stop.max = 1;
    @stop.step = 1;
    @stop.values = "Running,Stopped";
    @stop.label = "Stop";

    @time = 600 ms;
    @time.widget = 1;
    @time.min = 20ms;
    @time.max = 4000ms;
    @time.label = "Time";

node ionode {
    channels = 2;

    # The engine writes these every window. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    out0 = vsl->out;
    out1 = vsr->out;
};

# The ramp: an envelope's straight attack while `Stop' is up, and a
# millisecond's release when it comes down. `Stop' goes through a node so
# the trigger is a wire, which is what lets the envelope follow it.
node gate math::add {
    in0 = @stop;
    in1 = 0;
};

node ramp env::adsr {
    a = @time;
    d = 1 ms;
    s = 1;
    r = 1 ms;
    trigger = gate->out;
};

node vsl delay::varispeed {
    in = ionode->in0;
    speed = 1 - ramp->out;
};

node vsr delay::varispeed {
    in = ionode->in1;
    speed = 1 - ramp->out;
};

io ionode;
