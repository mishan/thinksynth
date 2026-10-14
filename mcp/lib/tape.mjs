// genwav's tape, read for a person: pitches as names, times as bars and
// beats, channels as the instruments the piece gave them.

const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

export function noteName (n)
{
    return NAMES[n % 12] + (Math.floor(n / 12) - 1);
}

// Bar and beat, both from 1, as a sequencer counts them.
export function barBeat (seconds, tempo, meter)
{
    const beats = seconds * tempo / 60;
    const bar = Math.floor(beats / meter + 1e-6);

    return { bar: bar + 1, beat: +(beats - bar * meter + 1).toFixed(3) };
}

export function readTape (text, { tempo, meter, instruments })
{
    const byEngine = new Map(instruments.map(i => [i.engine, i.name]));
    const events = [];

    for (const line of text.split('\n'))
    {
        const f = line.trim().split(/\s+/);

        if (f[0] === 'N')
        {
            const at = +f[1];

            events.push({ type: 'note', time: +at.toFixed(4),
                          ...barBeat(at, tempo, meter),
                          instrument: byEngine.get(+f[2]) ?? `channel ${+f[2] + 1}`,
                          note: noteName(+f[3]), vel: +f[4],
                          beats: +(+f[5] * tempo / 60).toFixed(3) });
        }
        else if (f[0] === 'C')
        {
            const at = +f[1];

            events.push({ type: 'chanarg', time: +at.toFixed(4),
                          ...barBeat(at, tempo, meter),
                          instrument: byEngine.get(+f[2]) ?? `channel ${+f[2] + 1}`,
                          name: f[3], value: +f[4] });
        }
    }

    return events;
}
