// 16-bit PCM WAV, the one format genwav writes, read into one Float32Array
// per channel.

import { readFile } from 'node:fs/promises';

export async function readWav (file)
{
    const b = await readFile(file);

    if (b.toString('ascii', 0, 4) !== 'RIFF' || b.toString('ascii', 8, 12) !== 'WAVE')
        throw new Error(`${file} is not a WAV file`);

    let at = 12, channels = 0, rate = 0, bits = 0;

    while (at + 8 <= b.length)
    {
        const id = b.toString('ascii', at, at + 4);
        const size = b.readUInt32LE(at + 4);

        if (id === 'fmt ')
        {
            channels = b.readUInt16LE(at + 10);
            rate = b.readUInt32LE(at + 12);
            bits = b.readUInt16LE(at + 22);
        }
        else if (id === 'data')
        {
            if (bits !== 16)
                throw new Error(`${file}: ${bits}-bit audio; only 16-bit is read`);

            const frames = Math.floor(Math.min(size, b.length - at - 8) / 2 / channels);
            const out = Array.from({ length: channels }, () => new Float32Array(frames));

            for (let i = 0; i < frames; i++)
                for (let c = 0; c < channels; c++)
                    out[c][i] = b.readInt16LE(at + 8 + (i * channels + c) * 2) / 32768;

            return { rate, channels: out };
        }

        at += 8 + size + (size & 1);
    }

    throw new Error(`${file} has no audio in it`);
}
