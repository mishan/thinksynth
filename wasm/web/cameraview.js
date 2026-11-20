/*
 * Copyright (C) 2004-2026 The thinksynth authors
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
 */

/*
 * cameraview.js -- what the camera sees, with what it makes of it on top.
 *
 * The picture mirrored, as the zones are (libthink/thMotion.h): the player's
 * top left is zone 1, top left on the screen. Over it the nine zones, each
 * shaded by how much it moves, with a bar along its foot that reaches a tick
 * where a strike starts, grey while the zone has struck and not yet gone
 * still, when it can't strike again, and a ring flashed when one does, as
 * wide as the strike was hard. So a swipe that didn't count shows how near
 * it came or that it came too soon, and a lamp or a window moving a zone
 * shows as that zone lit with no hand in it.
 *
 * Drawn on the camera's frames, and only while shown: a hidden pane costs
 * nothing but the strikes kept for its flashes.
 */

/* hid/thhCamera.h's HIT_ON, where a zone strikes, and STRIKE_FULL, where a
   strike is as hard as it gets: the bar is full there. */
const HIT_ON = 0.25;
const STRIKE_FULL = 0.5;

const FLASH_MS = 400;

/* The view on `canvas'. `shown()' says whether it is in front of anybody;
 * `onOpen(on)' is told when the camera opens and closes, for the page to
 * offer the view only while there is a camera.
 *
 * Returns { open, close, frame(video, levels, hits, down) }, for camera.js.
 */
export function createCameraView ({ canvas, shown, onOpen })
{
    const g = canvas.getContext('2d');
    const flashes = new Array(9).fill(null);
    let held = '', expire = 0;

    return {
        open: () =>
        {
            held = getComputedStyle(canvas).getPropertyValue('--held').trim();
            onOpen(true);
        },

        close: () =>
        {
            clearTimeout(expire);
            flashes.fill(null);
            g.clearRect(0, 0, canvas.width, canvas.height);
            onOpen(false);
        },

        frame (video, levels, hits, down)
        {
            const now = performance.now();

            for (const [zone, strike] of hits)
                flashes[zone] = { at: now, strike };

            /* Drawn once more when the last ring is out, should no frame
               come to do it: a camera that stops would leave it up. */
            const last = Math.max(...flashes.map((f) => f?.at ?? -FLASH_MS));

            clearTimeout(expire);
            if (now - last < FLASH_MS)
                expire = setTimeout(() => this.frame(video, levels, [], down),
                                    last + FLASH_MS - now);

            if (!shown() || !(video.videoWidth > 0))
                return;

            const w = video.videoWidth, h = video.videoHeight;

            if (canvas.width !== w || canvas.height !== h)
            {
                canvas.width = w;
                canvas.height = h;
            }

            g.save();
            g.scale(-1, 1);
            g.drawImage(video, -w, 0, w, h);
            g.restore();

            const zw = w / 3, zh = h / 3, bar = Math.max(4, zh / 16);

            g.lineWidth = Math.max(1, w / 320);
            for (let z = 0; z < 9; z++)
            {
                const x = (z % 3) * zw, y = Math.floor(z / 3) * zh;
                const full = Math.min(1, levels[z] / STRIKE_FULL);

                g.globalAlpha = 0.45 * full;
                g.fillStyle = held;
                g.fillRect(x, y, zw, zh);

                g.globalAlpha = 0.85;
                g.fillStyle = '#000';
                g.fillRect(x, y + zh - bar, zw, bar);
                g.fillStyle = down & (1 << z) ? '#808080'
                            : levels[z] >= HIT_ON ? '#fff' : held;
                g.fillRect(x, y + zh - bar, zw * full, bar);
                g.fillStyle = '#fff';
                g.fillRect(x + zw * HIT_ON / STRIKE_FULL - g.lineWidth / 2,
                           y + zh - bar * 2, g.lineWidth, bar * 2);

                g.globalAlpha = 0.6;
                g.strokeStyle = '#fff';
                g.strokeRect(x, y, zw, zh);

                const flash = flashes[z];
                const age = flash === null ? FLASH_MS : now - flash.at;

                if (age < FLASH_MS)
                {
                    g.globalAlpha = 1 - age / FLASH_MS;
                    g.strokeStyle = held;
                    g.lineWidth = Math.max(2, zw / 40) * (1 + 3 * flash.strike);
                    /* Above the bar and its tick, which it would hide. */
                    g.strokeRect(x + g.lineWidth / 2, y + g.lineWidth / 2,
                                 zw - g.lineWidth, zh - bar * 2 - g.lineWidth);
                    g.lineWidth = Math.max(1, w / 320);
                }
            }
            g.globalAlpha = 1;
        },
    };
}
