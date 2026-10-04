/* Sample packs: recordings the page downloads when somebody asks, and keeps.
 *
 * The site serves them under packs/ -- packs/index.json lists them, and
 * packs/<id>/pack.json lists a pack's wavs -- but nothing on the page
 * fetches them until a Download is pressed: they are tens of megabytes of
 * recorded instruments, and most visits play none of them. See
 * scripts/packs.py for what they are and where they come from.
 *
 * KEPT IN A CACHE OF THEIR OWN, `thinksynth-packs', and not the service
 * worker's: that one is replaced whole with every new build, and a pack is
 * the same pack whatever build plays it. The worker leaves this one alone
 * when it clears out old versions (serviceworker.js), the page asks the
 * browser to keep the site's storage rather than evict it under pressure,
 * and a pack downloaded once plays offline from then on.
 *
 * A PACK IS INSTALLED WHEN ITS pack.json IS IN THE CACHE, and that goes
 * in last: a download that stopped half way leaves wavs and no manifest,
 * which reads as not installed and is cleared by the next attempt. The
 * manifest carries the pack's `version', a hash of its files, and a kept
 * pack whose version is not the one offered is one to download again --
 * an instrument may name recordings the old one does not have.
 *
 * SERVED AS FLAC, about half the size of the WAV it decodes to, and kept
 * that way. Each file is listed under the .wav name its instrument plays and
 * the .flac it is fetched from; the browser's own decoder turns it into
 * samples when it is handed to the synth.
 *
 * Into the synth the way the shipped kit goes: as WAV bytes, through
 * synth.sample, under samples/<id>/. osc::sample reads a file it once
 * found missing again once new ones arrive (thUtil::dataFilesChanged), so
 * an instrument that was loaded before its pack is heard as soon as the
 * pack is in, without loading anything again.
 */

const CACHE = 'thinksynth-packs';

const manifestUrl = (id) => `packs/${id}/pack.json`;
const fileUrl = (f) => `packs/${f.url ?? f.name}`;

/* A file as the WAV osc::sample reads: as it came if it is one, decoded
   if it is FLAC, at the rate the packs are built at so nothing is
   resampled twice. */
async function wavBytes (f, bytes)
{
    if (!/\.flac$/.test(f.url ?? ''))
        return new Uint8Array(bytes);

    const audio = await new OfflineAudioContext(1, 1, 44100)
        .decodeAudioData(bytes);
    const pcm = audio.getChannelData(0);
    const out = new DataView(new ArrayBuffer(44 + 2 * pcm.length));
    const text = (at, s) =>
        [...s].forEach((c, i) => out.setUint8(at + i, c.charCodeAt(0)));

    text(0, 'RIFF');
    out.setUint32(4, 36 + 2 * pcm.length, true);
    text(8, 'WAVEfmt ');
    out.setUint32(16, 16, true);
    out.setUint16(20, 1, true);
    out.setUint16(22, 1, true);
    out.setUint32(24, audio.sampleRate, true);
    out.setUint32(28, 2 * audio.sampleRate, true);
    out.setUint16(32, 2, true);
    out.setUint16(34, 16, true);
    text(36, 'data');
    out.setUint32(40, 2 * pcm.length, true);

    for (let i = 0; i < pcm.length; i++)
        out.setInt16(44 + 2 * i,
                     Math.max(-32768, Math.min(32767,
                                               Math.round(pcm[i] * 32767))),
                     true);

    return new Uint8Array(out.buffer);
}

/* What the site offers, or nothing -- offline, or a build served without
   packs. */
export async function available ()
{
    try
    {
        const r = await fetch('packs/index.json', { cache: 'no-cache' });

        return r.ok ? await r.json() : [];
    }
    catch (e)
    {
        return [];
    }
}

async function manifests ()
{
    /* `has' before `open', which would make the cache: a page that never
       downloads a pack leaves no trace of the feature in its storage. */
    if (!('caches' in self) || !await caches.has(CACHE))
        return [];

    const cache = await caches.open(CACHE);
    const out = [];

    for (const req of await cache.keys())
        if (/\/packs\/[^/]+\/pack\.json$/.test(new URL(req.url).pathname))
            out.push(await (await cache.match(req)).json());

    return out;
}

/* The packs downloaded so far, id to version. */
export async function installed ()
{
    return new Map((await manifests()).map((m) => [m.id, m.version]));
}

/* Downloads a pack into the cache, `progress(done, total)' in bytes along
   the way, and hands it to `synth' if there is one. */
export async function download (id, synth, progress = () => {})
{
    const r = await fetch(manifestUrl(id), { cache: 'no-cache' });

    if (!r.ok)
        throw new Error(`${manifestUrl(id)}: ${r.status} ${r.statusText}`);

    const m = await r.json();
    const cache = await caches.open(CACHE);
    let done = 0;

    await remove(id);

    /* Four at a time: enough to fill a connection, few enough that a
       progress bar moves. */
    const queue = [...m.files];

    const worker = async () =>
    {
        for (let f; (f = queue.shift()) !== undefined;)
        {
            const url = fileUrl(f);
            const got = await fetch(url, { cache: 'no-cache' });

            if (!got.ok)
                throw new Error(`${url}: ${got.status} ${got.statusText}`);

            const bytes = await got.arrayBuffer();

            /* A copy into the cache: decoding detaches what it is given. */
            await cache.put(url, new Response(bytes.slice(0)));

            if (synth)
                synth.sample(`samples/${f.name}`, await wavBytes(f, bytes));
            done += f.bytes;
            progress(done, m.bytes);
        }
    };

    /* Every worker settled before any failure is reported: one that is
       still fetching would otherwise go on writing the cache and the
       progress under a retry that has already started. */
    const ends = await Promise.allSettled([worker(), worker(), worker(),
                                           worker()]);
    const failed = ends.find((e) => e.status === 'rejected');

    if (failed)
        throw failed.reason;

    await cache.put(manifestUrl(id), new Response(JSON.stringify(m)));

    navigator.storage?.persist?.().catch(() => {});

    return m;
}

export async function remove (id)
{
    if (!await caches.has(CACHE))
        return;

    const cache = await caches.open(CACHE);

    for (const req of await cache.keys())
        if (new URL(req.url).pathname.includes(`/packs/${id}/`))
            await cache.delete(req);
}

/* Every installed pack into `synth', at Start. */
export async function loadInstalled (synth)
{
    if (!('caches' in self) || !await caches.has(CACHE))
        return;

    const cache = await caches.open(CACHE);

    for (const m of await manifests())
        for (const f of m.files)
        {
            const hit = await cache.match(fileUrl(f));

            if (hit)
                synth.sample(`samples/${f.name}`,
                             await wavBytes(f, await hit.arrayBuffer()));
        }
}

/* The packs these graph texts play from: a sampled instrument names its
   files `<id>/...'. */
export function needed (texts, offered)
{
    const out = new Set();

    for (const p of offered)
        if (texts.some((t) => t.includes(`"${p.id}/`) ||
                              t.includes(` ${p.id}/`)))
            out.add(p.id);

    return out;
}
