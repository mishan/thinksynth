/*
 * Copyright (C) 2004-2026 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * mic.js -- the machine's own ear, onto the synth's input.
 *
 * A MediaStream into the worklet's input, which the graph reads as live0 on a
 * channel effect's io node (think.h, LIVEPREFIX). dsp/fx/vocoder-mic.dsp is
 * the graph that wants it: sixteen bands of the piece driven by whoever is in
 * the room.
 *
 * THE STREAM GOES TO THE WORKLET AND NOWHERE ELSE. It is deliberately not
 * connected to ctx.destination: that would be monitoring, and a microphone
 * monitored through speakers is the feedback loop below. What comes out is
 * what the graph made of it.
 *
 * ---- the constraints, which are the whole of this file's reason to exist ----
 *
 * echoCancellation, autoGainControl and noiseSuppression are all switched
 * *off*, and every one of them has to be. Their defaults are tuned for making
 * speech intelligible on a call, and each breaks a vocoder in its own way:
 *
 *   autoGainControl fights the envelope. A vocoder measures the loudness of
 *      each band of the modulator; a gain control that is also adjusting
 *      loudness, on its own schedule, is a second thing moving the number the
 *      first thing is trying to read.
 *
 *   echoCancellation removes the carrier. It removes whatever in the input
 *      correlates with the output -- and for a vocoder the output *is* the
 *      input's spectrum wearing the carrier, so the thing it cancels is the
 *      signal. (It would also stop the feedback loop, which is the awkward
 *      part: the fix for howl and the fix for a working vocoder are opposites.
 *      Use headphones.)
 *
 *   noiseSuppression is a spectral gate in front of everything. Consonants are
 *      noise above where any formant is -- `s', `t', `f', `sh' -- which is
 *      exactly what a speech denoiser is built to remove, and exactly what
 *      fx/vocoder.dsp passes through unvocoded to get its teeth.
 *
 * And a fourth reason that has nothing to do with taste: all three are
 * implemented differently in Chromium and in Firefox, so leaving them on makes
 * the capture browser-dependent. Every gate in wasm/web/ rests on two browsers
 * agreeing about what a module did with the same input.
 *
 * They are requested rather than guaranteed -- a constraint is a wish -- so
 * what was actually granted is read back and reported, because a browser that
 * quietly kept its AGC on is worth knowing about before spending an hour
 * wondering why the consonants are mushy.
 *
 * No channelCount constraint: a track with two channels is summed to one in
 * the worklet, which is what the engine's capture is anyway.
 */

/* What the page asks for. Spelled once, because it is read back against what
   was granted and the two lists have to be the same list. */
const WANT = {
    echoCancellation: false,
    autoGainControl: false,
    noiseSuppression: false,
};

/* Names for the log, in the order a person would want to read them. */
const PROCESSING = Object.keys(WANT);

/* True where the browser can be asked at all. A page served over plain http
   from anywhere but localhost has no mediaDevices, and the failure is worth
   telling apart from a refusal: one is fixable by the user and one is not. */
export function micAvailable ()
{
    return typeof navigator !== 'undefined' &&
           navigator.mediaDevices !== undefined &&
           typeof navigator.mediaDevices.getUserMedia === 'function';
}

/* Opens the machine's input and connects it to `node', which is the synth's
 * AudioWorkletNode (host.js gives it one input for this).
 *
 * Resolves to { label, processing, stream, close }. `label' is the device's own
 * name where the browser gives one -- it does not before permission is granted,
 * which is why this is read after. `processing' says which of the three
 * switches the browser actually honoured. `close' releases the device.
 *
 * Rejects with a message fit to put on the page: a refusal, an absent device
 * and an insecure context are three different things a person can do something
 * different about.
 */
export async function openMic (ctx, node, { deviceId = null } = {})
{
    if (!micAvailable())
        throw new Error('this browser will not offer a microphone here -- ' +
                        'a page needs https, or localhost, to ask for one');

    const audio = { ...WANT };

    if (deviceId)
        audio.deviceId = { exact: deviceId };

    let stream;

    try
    {
        stream = await navigator.mediaDevices.getUserMedia({ audio });
    }
    catch (e)
    {
        /* The names are the spec's and the distinction is the one that
           matters: told no, or nothing there to say yes. */
        if (e.name === 'NotAllowedError' || e.name === 'SecurityError')
            throw new Error('the microphone was refused');

        if (e.name === 'NotFoundError' || e.name === 'OverconstrainedError')
            throw new Error('no microphone this browser will offer');

        throw new Error(`the microphone did not open: ${e.message || e.name}`);
    }

    const [track] = stream.getAudioTracks();

    if (track === undefined)
    {
        stream.getTracks().forEach((t) => t.stop());
        throw new Error('the stream that opened carries no audio');
    }

    /* What was granted, against what was asked for. getSettings() is the
       browser's answer and not the page's wish; a switch it does not report at
       all is one it does not have, which is not the same as one it ignored. */
    const got = typeof track.getSettings === 'function'
        ? track.getSettings() : {};
    const processing = {};

    for (const name of PROCESSING)
        processing[name] = (name in got) ? got[name] : null;

    /* A source node has to outlive this call or it is collected and the input
       goes quiet, so it is held in what comes back. */
    const source = ctx.createMediaStreamSource(stream);

    /* To the worklet's input and to nothing else. See the head. */
    source.connect(node);

    let open = true;

    return {
        label: track.label || 'microphone',
        processing,
        stream,

        /* Which of the three the browser did not do as asked, as text for the
           page. Empty when everything was honoured, which is the usual case
           and the one that should say nothing. */
        warnings ()
        {
            return PROCESSING
                .filter((name) => processing[name] === true)
                .map((name) => `${name} is on and was asked to be off`);
        },

        close ()
        {
            if (!open)
                return;

            open = false;

            source.disconnect();
            stream.getTracks().forEach((t) => t.stop());
        },
    };
}
