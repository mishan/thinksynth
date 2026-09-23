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
 * A version the worker has fetched waits until it is asked for. It is
 * asked for here as soon as it is waiting -- which is not always at load:
 * the load is what finds a new sw.js, and the new version spends the next
 * few seconds downloading the site before it waits. Asking only at load
 * found it still installing, so a refresh after a deploy did nothing and
 * only a second one updated.
 *
 * Asked for straight away only while the page has nothing going: taking
 * over reloads the page, and a reload under somebody who has started the
 * synth or joined a room loses what they were doing. Otherwise `offer' is
 * handed the asking, for a button; and so it is when the worker refuses,
 * which it does while another window of the site is open.
 *
 * Called once the page has no fetch of its own in flight. Chromium holds
 * the new version back while the running one has requests in flight, and
 * one asked for with a request in flight is held for as long as the page
 * stays open, long after the requests are done.
 *
 *   busy()     whether the page has something going a reload would lose
 *   offer(go)  a version is ready and not taken; go() asks, and resolves
 *              to whether the worker agreed
 */

/* How often a page left open looks for a new version: when it comes back
   to the front, and not more than hourly. The installed app is a window
   somebody may not reload for days. */
const RECHECK = 60 * 60 * 1000;

export async function keepOffline ({ busy = () => false,
                                     offer = () => {} } = {})
{
    if (!('serviceWorker' in navigator) ||
        (location.protocol !== 'https:' &&
         !new URLSearchParams(location.search).has('sw')))
        return;

    const reg = await navigator.serviceWorker.register('sw.js');

    /* A page with no worker yet is the first install, which claims it
       and has nothing older to be updated from. */
    if (navigator.serviceWorker.controller === null)
        return;

    whenWaiting(reg, (next) =>
    {
        const go = () => ask(next);

        if (busy())
            offer(go);
        else
            go().then((granted) => granted || offer(go));
    });

    let checked = Date.now();

    document.addEventListener('visibilitychange', () =>
    {
        if (document.visibilityState !== 'visible' ||
            Date.now() - checked < RECHECK)
            return;

        checked = Date.now();
        reg.update().catch(() => {});
    });
}

/* Every version that comes to be waiting: the one already waiting, the one
   installing now, and any the browser finds later. Each once. */
function whenWaiting (reg, then)
{
    let told = null;

    const tell = (worker) =>
    {
        if (worker !== null && worker !== told)
        {
            told = worker;
            then(worker);
        }
    };

    const track = (worker) =>
    {
        if (worker === null)
            return;

        if (worker.state === 'installed')
            tell(worker);
        else
            worker.addEventListener('statechange', () =>
            {
                if (worker.state === 'installed')
                    tell(worker);
            });
    };

    tell(reg.waiting);
    track(reg.installing);
    reg.addEventListener('updatefound', () => track(reg.installing));
}

/* The page loads again once the new version has it. Once, however many
   times it was asked for. */
let reloading = false;

/* Asks the waiting version to take over, and resolves to whether the
   worker agreed. */
function ask (next)
{
    if (!reloading)
    {
        reloading = true;
        navigator.serviceWorker.addEventListener('controllerchange',
                                                 () => location.reload(),
                                                 { once: true });
    }

    return new Promise((resolve) =>
    {
        const answer = (e) =>
        {
            if (e.data !== 'granted' && e.data !== 'refused')
                return;

            navigator.serviceWorker.removeEventListener('message', answer);
            resolve(e.data === 'granted');
        };

        navigator.serviceWorker.addEventListener('message', answer);
        next.postMessage('activate');
    });
}

/* The browser's install question, on a button of the page's.
 *
 * Chromium asks from an icon at the end of its address bar, which is easy
 * to miss, and it also fires `beforeinstallprompt' at a page it would
 * install. Holding on to that event is what lets a button ask the same
 * question. Nothing else fires it -- Safari installs from Share, Add to
 * Home Screen -- and Chromium stops once the page is installed, so the
 * button is only ever there when pressing it would do something.
 *
 * Called at load, since the event can come before anything else is done. */
export function offerInstall (button)
{
    let asking = null;

    addEventListener('beforeinstallprompt', (e) =>
    {
        /* The button instead of the mini-infobar a phone would show. */
        e.preventDefault();
        asking = e;
        button.hidden = false;
    });

    button.addEventListener('click', async () =>
    {
        if (asking === null)
            return;

        /* One use each: a dismissed prompt is not shown again, and
           Chromium fires a fresh event if it will ask again. */
        const it = asking;

        asking = null;
        button.hidden = true;
        await it.prompt();
    });

    addEventListener('appinstalled', () =>
    {
        asking = null;
        button.hidden = true;
    });
}
