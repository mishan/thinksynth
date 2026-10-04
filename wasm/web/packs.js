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
 * in last. The manifest carries the pack's `version', a hash of its files,
 * and every file is kept under its version (`?v='), so an update writes
 * the new files beside the old ones, puts the new manifest over the old
 * only once all of them are in, and only then clears the old files: an
 * update that fails half way leaves the pack that worked. A kept pack
 * whose version is not the one offered is one to download again -- an
 * instrument may name recordings the old one does not have. One download
 * per pack at a time; asking again while one runs joins it.
 *
 * SERVED AS FLAC, about half the size of the WAV it decodes to, and kept
 * that way. Each file is listed under the .wav name its instrument plays and
 * the .flac it is fetched from; the browser's own decoder turns it into
 * samples when it is handed to the synth.
 *
 * Into the synth the way the shipped kit goes: as WAV bytes, through
 * synth.sample, under samples/<id>/, once a pack is complete -- into
 * whichever synth there is then, which may have started while it was
 * downloading. osc::sample reads a file again once the host names it as
 * written (thUtil::dataFileChanged), so an instrument loaded before its
 * pack, or playing an older version, is heard with the new files without
 * loading anything again.
 */

const CACHE = 'thinksynth-packs';

const manifestUrl = (id) => `packs/${id}/pack.json`;

/* A file's key in the cache: where it is served, and the version of the
   pack it belongs to. */
const fileKey = (m, f) => `packs/${f.url ?? f.name}?v=${m.version ?? ''}`;

/* A name to hand the synth: one under its pack, going nowhere else. */
const safe = (m, f) => f.name.startsWith(`${m.id}/`) &&
                       !f.name.split('/').includes('..');

/* A file as the WAV osc::sample reads: as it came if it is one, decoded
   if it is FLAC, at the rate the packs are built at so nothing is
   resampled twice. */
export async function wavBytes (f, bytes)
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

/* `has' before `open', which would make the cache: a page that never
   downloads a pack leaves no trace of the feature in its storage. */
async function cache ()
{
    return ('caches' in self) && await caches.has(CACHE)
        ? caches.open(CACHE) : null;
}

/* The packs downloaded so far, id to manifest. */
export async function installed ()
{
    const c = await cache();
    const out = new Map();

    if (c === null)
        return out;

    for (const req of await c.keys())
        if (/\/packs\/[^/]+\/pack\.json$/.test(new URL(req.url).pathname))
        {
            const m = await (await c.match(req)).json();

            out.set(m.id, m);
        }

    return out;
}

/* The downloads under way, by pack. */
const running = new Map();

/* Downloads a pack into the cache, `progress(done, total)' in bytes along
   the way, and resolves to its manifest once it is complete. A second ask
   for the same pack while this runs gets this one. */
export function download (id, progress = () => {})
{
    if (!running.has(id))
        running.set(id, fetchPack(id, progress)
            .finally(() => running.delete(id)));

    return running.get(id);
}

async function fetchPack (id, progress)
{
    const r = await fetch(manifestUrl(id), { cache: 'no-cache' });

    if (!r.ok)
        throw new Error(`${manifestUrl(id)}: ${r.status} ${r.statusText}`);

    const m = await r.json();
    const c = await caches.open(CACHE);
    const kept = (await installed()).get(id);
    const queue = m.files.filter((f) => safe(m, f));
    let done = 0;

    /* Four at a time: enough to fill a connection, few enough that a
       progress bar moves. */
    const worker = async () =>
    {
        for (let f; (f = queue.shift()) !== undefined;)
        {
            const url = `packs/${f.url ?? f.name}`;
            const got = await fetch(url, { cache: 'no-cache' });

            if (!got.ok)
                throw new Error(`${url}: ${got.status} ${got.statusText}`);

            await c.put(fileKey(m, f), new Response(await got.arrayBuffer()));
            done += f.bytes;
            progress(done, m.bytes);
        }
    };

    /* Every worker settled before any failure is reported, so nothing is
       still writing when the failure is. */
    const ends = await Promise.allSettled([worker(), worker(), worker(),
                                           worker()]);
    const failed = ends.find((e) => e.status === 'rejected');

    if (failed)
    {
        /* What this attempt wrote, unless it is the version that is kept,
           whose files are the same ones. */
        if (kept?.version !== m.version)
            await clear(c, id, (v) => v === m.version);

        throw failed.reason;
    }

    await c.put(manifestUrl(id), new Response(JSON.stringify(m)));
    await clear(c, id, (v) => v !== m.version);

    navigator.storage?.persist?.().catch(() => {});

    return m;
}

/* A pack's files whose version `which' picks. */
async function clear (c, id, which)
{
    for (const req of await c.keys())
    {
        const url = new URL(req.url);

        if (url.pathname.includes(`/packs/${id}/`) &&
            !url.pathname.endsWith('/pack.json') &&
            which(url.searchParams.get('v') ?? ''))
            await c.delete(req);
    }
}

export async function remove (id)
{
    const c = await cache();

    if (c === null)
        return;

    for (const req of await c.keys())
        if (new URL(req.url).pathname.includes(`/packs/${id}/`))
            await c.delete(req);
}

/* A kept pack into `synth'. */
export async function load (m, synth)
{
    const c = await cache();

    if (c === null)
        return;

    for (const f of m.files.filter((f) => safe(m, f)))
    {
        const hit = await c.match(fileKey(m, f));

        if (hit)
            synth.sample(`samples/${f.name}`,
                         await wavBytes(f, await hit.arrayBuffer()));
    }
}

/* Every kept pack into `synth', at Start, side by side. */
export async function loadInstalled (synth)
{
    await Promise.all([...(await installed()).values()]
        .map((m) => load(m, synth)));
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
