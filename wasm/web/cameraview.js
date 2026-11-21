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
 * Returns { open, close, frame(video, levels, hits, down), hands(video,
 * left, right, points) }, for camera.js: the zones as the air harp plays,
 * or the hands as the theremin does.
 */
export function createCameraView ({ canvas, shown, onOpen })
{
    const g = canvas.getContext('2d');
    const flashes = new Array(9).fill(null);
    let held = '', expire = 0;

    /* The camera's picture, mirrored, sized to it; false while there is
       nobody to see it or nothing to draw. */
    const picture = (video) =>
    {
        if (!shown() || !(video.videoWidth > 0))
            return false;

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
        return true;
    };

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

            if (!picture(video))
                return;

            const w = canvas.width, h = canvas.height;

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

        /* The tracker's points; each hand as the theremin has it, a ring
           filled while pinched; the right hand's pitch up the right edge,
           on the profile's 15 steps while `snapped'; and the left hand's
           volume up the left. */
        hands (video, left, right, points, snapped)
        {
            if (!picture(video))
                return;

            const w = canvas.width, h = canvas.height;
            const steps = 15, bar = Math.max(6, w / 80), r = w / 30;

            g.fillStyle = '#fff';
            for (let p = 0; p + 44 <= points.length; p += 44)
                for (let i = 0; i < 21; i++)
                    g.fillRect((1 - points[p + i]) * w - 2, points[p + 21 + i] * h - 2, 4, 4);

            g.globalAlpha = 0.6;
            for (let k = 0; snapped && k <= steps; k++)
                g.fillRect(w - bar, (1 - k / steps) * h - 1, bar, 2);
            g.globalAlpha = 0.85;
            g.fillStyle = held;
            if (right)
                g.fillRect(w - 2 * bar, (1 - (snapped ? Math.round(right[1] * steps) / steps
                                                      : right[1])) * h - 3, 2 * bar, 6);
            if (left)
                g.fillRect(0, (1 - left[1]) * h, bar, left[1] * h);

            g.lineWidth = Math.max(2, w / 160);
            g.font = `${Math.round(r)}px sans-serif`;
            g.textAlign = 'center';
            g.textBaseline = 'middle';
            for (const [hand, name] of [[left, 'L'], [right, 'R']])
            {
                if (!hand)
                    continue;

                const x = hand[0] * w, y = (1 - hand[1]) * h;

                g.beginPath();
                g.arc(x, y, r, 0, 2 * Math.PI);
                g.strokeStyle = held;
                g.stroke();
                if (hand[3])
                    g.fill();
                g.fillStyle = '#fff';
                g.fillText(name, x, y);
                g.fillStyle = held;
            }
            g.globalAlpha = 1;
        },
    };
}
