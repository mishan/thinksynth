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
 * serviceworker.js -- the site, offline and on one build.
 *
 * Not served as it is: precache.mjs puts VERSION and FILES in front of it
 * and writes the result to the build directory as sw.js. FILES is every
 * file either page can ask for -- the solo page and the room page, their
 * modules, the wasm, and every .dsp, .gen and .patch with the indexes
 * naming them -- and VERSION is a hash of their contents. A build that
 * changes any of them changes sw.js, and a changed sw.js is the only thing
 * that makes a browser install a new worker.
 *
 * One cache per VERSION, filled whole on install, and every request for
 * one of FILES answered from it: a page, its worklet and its mirror worker
 * all load from the one build, so the module, the worklet's glue and the
 * .dsp files the module parses always agree. The room page is kept for
 * that and not for being offline, where it has no relay: it loads the
 * same worklet, mirror and wasm as the solo page, and its mirror worker's
 * requests carry nothing to say which page made it, so the only way its
 * bundle agrees with them is to come from the same cache. What is left to
 * the network is config.json, which says where the relay is and no build
 * of the page depends on, and the bundle's source map.
 *
 * A new version waits. Activating it under a page that is running would
 * hand that page's next .dsp fetch to a different build from the wasm it
 * already loaded. Either page asks for it at load instead (offline.js),
 * and it is granted only if that page is the one window of the site open,
 * since then nothing has been loaded from the old cache that the new one
 * could disagree with.
 *
 * The site is scoped to the directory sw.js is served from. On GitHub
 * Pages that is /thinksynth/ on an origin other repositories share, so
 * the caches this deletes are its own by name and nobody else's.
 */

const PREFIX = 'thinksynth-';
const CACHE = PREFIX + VERSION;

/* Every precached file by absolute URL, without a query. */
const PRECACHED = new Set(FILES.map((f) => new URL(f, self.location).href));

/* The site's root, which a navigation reaches as the directory. */
const ROOT = new URL('./', self.location).href;
const INDEX = new URL('index.html', self.location).href;

self.addEventListener('install', (e) =>
{
    /* `reload': the bytes from the server and not from the HTTP cache,
       which could still hold a file from the build before this one. */
    e.waitUntil(caches.open(CACHE).then((cache) =>
        cache.addAll([...PRECACHED].map(
            (url) => new Request(url, { cache: 'reload' })))));
});

self.addEventListener('activate', (e) =>
{
    e.waitUntil((async () =>
    {
        for (const name of await caches.keys())
        {
            if (name.startsWith(PREFIX) && name !== CACHE)
                await caches.delete(name);
        }

        /* The first install has no old version to disagree with: the page
           that registered it came from the network, and from this build
           unless a deploy landed between the two. Claiming it is what lets
           a page opened once work offline without a second load. */
        await self.clients.claim();
    })());
});

/* A page asking for the waiting version, at load. Granted only to the one
   window of this site that is open, of either page: both run from the
   cache. Clients outside the scope are other sites on a shared origin,
   and are not counted. */
self.addEventListener('message', (e) =>
{
    if (e.data !== 'activate')
        return;

    e.waitUntil((async () =>
    {
        const all = await self.clients.matchAll(
            { type: 'window', includeUncontrolled: true });
        const ours = all.filter((c) => c.url.startsWith(ROOT));

        if (ours.length <= 1)
            await self.skipWaiting();
    })());
});

self.addEventListener('fetch', (e) =>
{
    if (e.request.method !== 'GET')
        return;

    const url = new URL(e.request.url);

    url.search = '';
    url.hash = '';

    /* The page by any of its names: the directory, index.html, either
       with the query a test or a link adds. */
    const key = url.href === ROOT ? INDEX : url.href;

    if (!PRECACHED.has(key))
        return;

    /* A miss is a cache the browser evicted under storage pressure, which
       it may do; the network is what is left. */
    e.respondWith(caches.open(CACHE).then((cache) => cache.match(key))
        .then((hit) => hit ?? fetch(e.request)));
});
