/*
 * cairo2d -- the replayer.
 *
 * One function, and it is the whole browser half of the stand-in: take the
 * list a cairo_t recorded and make the same calls on a
 * CanvasRenderingContext2D. Every op is one or two Canvas2D calls, because
 * the vocabulary was chosen to be the intersection of the two.
 *
 * The three places the two models differ, and what is done about them:
 *
 *   - cairo's fill and stroke clear the current path and Canvas2D's do
 *     not, so a beginPath() is owed after either, and is paid the next
 *     time a path op arrives. The _preserve pair owe nothing.
 *   - cairo has new_sub_path, which says "the next arc starts where it
 *     starts rather than joining what came before". Canvas2D has no such
 *     call, so the arc's first point is moved to instead -- which is what
 *     cairo does internally.
 *   - cairo's paint fills the clip region with the source. With a colour
 *     that is a fillRect over the whole surface under the base transform,
 *     which the clip cuts down; with a surface it is a drawImage at the
 *     source's origin under the current transform.
 *
 * The op numbers below are cairo2d.h's. A test holds them against the
 * module's own cairo2d_op_name() so that the two lists cannot drift.
 *
 * Public domain, or CC0 where that is not a thing. Take it.
 */

export const OPS = {
    SAVE: 1,
    RESTORE: 2,
    TRANSLATE: 3,
    SCALE: 4,

    NEW_PATH: 5,
    NEW_SUB_PATH: 6,
    MOVE_TO: 7,
    LINE_TO: 8,
    CURVE_TO: 9,
    ARC: 10,
    RECTANGLE: 11,
    CLOSE_PATH: 12,

    SET_SOURCE_RGBA: 13,
    SET_SOURCE_SURFACE: 14,
    SET_LINE_WIDTH: 15,
    SET_LINE_CAP: 16,
    SET_DASH: 17,
    SET_FILTER: 18,

    FILL: 19,
    FILL_PRESERVE: 20,
    STROKE: 21,
    STROKE_PRESERVE: 22,
    PAINT: 23,
    CLIP: 24,

    SET_FONT: 25,
    SHOW_TEXT: 26,
};

/* How many operands follow each opcode; -1 is SET_DASH, which says its
   own count first. Indexed by opcode, so entry 0 is not an op. */
export const ARITY = [
    -2, 0, 0, 2, 2,
    0, 0, 2, 2, 6, 5, 4, 0,
    4, 3, 1, 1, -1, 1,
    0, 0, 0, 0, 0, 0,
    1, 3,
];

export const OP_NAMES = (() => {
    const names = [];

    for (const [name, op] of Object.entries(OPS))
        names[op] = name;

    return names;
})();

const LINE_CAPS = ['butt', 'round', 'square'];

/*
 * A surface's pixels, as something drawImage will take.
 *
 * The module's heap holds them in cairo's own order -- four bytes a pixel,
 * blue first, which is what a little-endian machine's 0x00RRGGBB looks
 * like a byte at a time -- and ImageData wants red first. So the swap is
 * here, once per surface per frame, and the result is put on a scratch
 * canvas because putImageData ignores the transform and the clip and
 * drawImage honours both.
 */
function surfaceCanvas (surface, cache) {
    const key = surface.index;

    if (cache.has(key))
        return cache.get(key);

    const { width, height, stride, data } = surface;
    const image = new ImageData(width, height);
    const out = image.data;

    for (let y = 0; y < height; y++) {
        let src = y * stride;
        let dst = y * width * 4;

        for (let x = 0; x < width; x++, src += 4, dst += 4) {
            out[dst + 0] = data[src + 2];
            out[dst + 1] = data[src + 1];
            out[dst + 2] = data[src + 0];
            out[dst + 3] = 255;
        }
    }

    const canvas = typeof OffscreenCanvas !== 'undefined'
        ? new OffscreenCanvas(width, height)
        : Object.assign(document.createElement('canvas'), { width, height });

    canvas.getContext('2d').putImageData(image, 0, 0);
    cache.set(key, canvas);

    return canvas;
}

/*
 * Replay `ops' onto `ctx'.
 *
 *   ops       a Float32Array, or anything indexable by number
 *   strings   the string table: SET_FONT and SHOW_TEXT index into it
 *   surfaces  the surface table: { index, width, height, stride, data }
 *   opts      { width, height, dpr, clear }, the canvas's CSS size, the
 *             device pixel ratio the element was sized at, and whether to
 *             start from an empty canvas (true by default)
 *
 * Returns the number of ops replayed, which is what a caller with nothing
 * else to look at can assert on.
 */
export function replay (ctx, ops, strings, surfaces, opts = {}) {
    const width = opts.width ?? 0;
    const height = opts.height ?? 0;
    const dpr = opts.dpr ?? 1;
    const n = ops.length;

    const canvases = new Map();

    let source = null;          /* null is a colour; else the surface op */
    let needPath = true;
    let newSubPath = false;
    let count = 0;

    const stack = [];

    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    if (opts.clear !== false)
        ctx.clearRect(0, 0, width, height);

    const path = () => {
        if (needPath) {
            ctx.beginPath();
            needPath = false;
        }
    };

    for (let i = 0; i < n; ) {
        const op = ops[i++];

        count++;

        switch (op) {
        case OPS.SAVE:
            stack.push(source);
            ctx.save();
            break;

        case OPS.RESTORE:
            source = stack.length > 0 ? stack.pop() : source;
            ctx.restore();
            break;

        case OPS.TRANSLATE:
            ctx.translate(ops[i], ops[i + 1]);
            i += 2;
            break;

        case OPS.SCALE:
            ctx.scale(ops[i], ops[i + 1]);
            i += 2;
            break;

        case OPS.NEW_PATH:
            ctx.beginPath();
            needPath = false;
            newSubPath = false;
            break;

        case OPS.NEW_SUB_PATH:
            newSubPath = true;
            break;

        case OPS.MOVE_TO:
            path();
            ctx.moveTo(ops[i], ops[i + 1]);
            newSubPath = false;
            i += 2;
            break;

        case OPS.LINE_TO:
            path();
            ctx.lineTo(ops[i], ops[i + 1]);
            i += 2;
            break;

        case OPS.CURVE_TO:
            path();
            ctx.bezierCurveTo(ops[i], ops[i + 1], ops[i + 2], ops[i + 3],
                              ops[i + 4], ops[i + 5]);
            i += 6;
            break;

        case OPS.ARC: {
            const [xc, yc, r, a1, a2] =
                [ops[i], ops[i + 1], ops[i + 2], ops[i + 3], ops[i + 4]];

            path();

            if (newSubPath) {
                ctx.moveTo(xc + r * Math.cos(a1), yc + r * Math.sin(a1));
                newSubPath = false;
            }

            ctx.arc(xc, yc, r, a1, a2);
            i += 5;
            break;
        }

        case OPS.RECTANGLE:
            path();
            ctx.rect(ops[i], ops[i + 1], ops[i + 2], ops[i + 3]);
            newSubPath = false;
            i += 4;
            break;

        case OPS.CLOSE_PATH:
            path();
            ctx.closePath();
            break;

        case OPS.SET_SOURCE_RGBA: {
            const css = `rgba(${Math.round(ops[i] * 255)}, ` +
                        `${Math.round(ops[i + 1] * 255)}, ` +
                        `${Math.round(ops[i + 2] * 255)}, ${ops[i + 3]})`;

            ctx.fillStyle = css;
            ctx.strokeStyle = css;
            source = null;
            i += 4;
            break;
        }

        case OPS.SET_SOURCE_SURFACE:
            source = { surface: ops[i], x: ops[i + 1], y: ops[i + 2] };
            i += 3;
            break;

        case OPS.SET_LINE_WIDTH:
            ctx.lineWidth = ops[i++];
            break;

        case OPS.SET_LINE_CAP:
            ctx.lineCap = LINE_CAPS[ops[i++]] ?? 'butt';
            break;

        case OPS.SET_DASH: {
            const dashes = [];
            const many = ops[i++];

            for (let k = 0; k < many; k++)
                dashes.push(ops[i++]);

            ctx.setLineDash(dashes);
            ctx.lineDashOffset = ops[i++];
            break;
        }

        case OPS.SET_FILTER:
            ctx.imageSmoothingEnabled = ops[i++] !== 0;
            break;

        case OPS.FILL:
            ctx.fill();
            needPath = true;
            break;

        case OPS.FILL_PRESERVE:
            ctx.fill();
            break;

        case OPS.STROKE:
            ctx.stroke();
            needPath = true;
            break;

        case OPS.STROKE_PRESERVE:
            ctx.stroke();
            break;

        case OPS.PAINT:
            if (source === null) {
                ctx.save();
                ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
                ctx.fillRect(0, 0, width, height);
                ctx.restore();
            } else {
                const surface = surfaces[source.surface];

                if (surface !== undefined)
                    ctx.drawImage(surfaceCanvas(surface, canvases),
                                  source.x, source.y);
            }

            break;

        case OPS.CLIP:
            path();
            ctx.clip();
            needPath = true;
            break;

        case OPS.SET_FONT:
            ctx.font = strings[ops[i++]];
            break;

        case OPS.SHOW_TEXT:
            ctx.fillText(strings[ops[i]], ops[i + 1], ops[i + 2]);
            i += 3;
            break;

        default:
            /* Not reachable from a list this build recorded: an op the
               replayer does not know is a cairo2d.h and a replay.js that
               have parted, and there is a test for exactly that. Stop
               rather than read the operands as opcodes. */
            ctx.restore();
            throw new Error(`cairo2d: unknown op ${op} at word ${i - 1}`);
        }
    }

    ctx.restore();

    return count;
}
