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
 * offline.js -- the service worker, from either page.
 *
 * The worker (serviceworker.js) keeps both pages, the solo page and the
 * room, for one build at a time, and either page registers it. Over https
 * only: that is where the site is published, and a worker on serve.mjs's
 * http://localhost would answer a rebuilt tree from the cache of the last
 * one. `?sw' asks for it anyway, for the harness that tests it.
 *
 * A version the worker has fetched waits until it is asked for, and it is
 * asked for here, at load, before anything has been played -- see the
 * worker for why not later. If it takes over, the page loads again from
 * it.
 */

export async function keepOffline ()
{
    if (!('serviceWorker' in navigator) ||
        (location.protocol !== 'https:' &&
         !new URLSearchParams(location.search).has('sw')))
        return;

    const reg = await navigator.serviceWorker.register('sw.js');

    if (reg.waiting === null || navigator.serviceWorker.controller === null)
        return;

    navigator.serviceWorker.addEventListener('controllerchange',
                                             () => location.reload(),
                                             { once: true });
    reg.waiting.postMessage('activate');
}
