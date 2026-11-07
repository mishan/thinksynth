// Runs the tree's own harnesses. Every musical answer comes from them, so
// the server, the command line and the program cannot disagree about what a
// piece or an instrument means; this file only finds them and moves bytes.

import { execFile } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdir, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)),
                                 '..', '..');
export const BUILD = process.env.THINK_BUILD
    ? path.resolve(process.env.THINK_BUILD) : path.join(REPO, 'build');
export const PLUGINS = path.join(BUILD, 'plugins');
export const OUT = process.env.THINK_MCP_OUT
    ? path.resolve(process.env.THINK_MCP_OUT)
    : path.join(os.tmpdir(), 'thinksynth-mcp');

export function harness (name)
{
    const bin = path.join(BUILD, 'scripts', name);

    if (!existsSync(bin))
        throw new Error(`${bin} is missing: build the tree first ` +
                        `(cmake -S . -B build && cmake --build build), or ` +
                        `set THINK_BUILD to a build directory`);

    return bin;
}

// From the top of the tree, which is where a piece's `dsp "ts1.dsp"` and
// dspcatalog's corpus are found.
export function run (name, args, { timeout = 600_000, signal } = {})
{
    return new Promise((resolve) =>
    {
        execFile(harness(name), args,
                 { cwd: REPO, timeout, signal, maxBuffer: 256 << 20 },
                 (error, stdout, stderr) =>
                 {
                     resolve({ code: error ? (error.code ?? 1) : 0,
                               killed: Boolean(error?.killed),
                               stdout, stderr });
                 });
    });
}

export function json (result, what)
{
    try
    {
        return JSON.parse(result.stdout);
    }
    catch
    {
        throw new Error(`${what} gave no JSON (exit ${result.code}): ` +
                        (result.stderr || result.stdout).slice(0, 2000));
    }
}

let made = 0;
const kept = [], evicted = [], busy = new Map();
const KEEP = Math.max(1, Number(process.env.THINK_MCP_KEEP) || 32);

// Told the directory a call made is being taken away, so what pointed into
// it can be forgotten too.
export function onEvict (fn)
{
    evicted.push(fn);
}

// One tool call's directories: each made for it is in use until it ends,
// and gone then unless kept -- only what a person may come back for, a
// render or an audition, counts against KEEP.
export class Call
{
    constructor (signal)
    {
        this.signal = signal;
        this.made = [];
        this.used = [];
    }

    async workdir (kind)
    {
        const dir = path.join(OUT, `${kind}-${Date.now()}-${process.pid}-${made++}`);

        await mkdir(dir, { recursive: true });
        this.made.push(dir);
        this.use(dir);

        return dir;
    }

    keep (dir)
    {
        kept.push(dir);
    }

    use (dir)
    {
        this.used.push(dir);
        busy.set(dir, (busy.get(dir) ?? 0) + 1);
    }

    run (name, args, options = {})
    {
        return run(name, args, { ...options, signal: this.signal });
    }

    async end ()
    {
        for (const dir of this.used)
            busy.get(dir) > 1 ? busy.set(dir, busy.get(dir) - 1) : busy.delete(dir);

        await Promise.all(this.made.filter(dir => !kept.includes(dir))
                                   .map(dir => rm(dir, { recursive: true, force: true })));
        await prune();
    }
}

// Only the last KEEP kept directories stay, less any a running call is
// still using, which go after it. Taken off the list before the first
// await, so two calls pruning at once never pick the same one.
async function prune ()
{
    const gone = [];

    for (let i = 0; i < kept.length && kept.length > KEEP; )
        if (busy.has(kept[i]))
            i++;
        else
            gone.push(...kept.splice(i, 1));

    for (const dir of gone)
        evicted.forEach(fn => fn(dir));

    await Promise.all(gone.map(dir => rm(dir, { recursive: true, force: true })));
}

// A path a call names, from the top of the tree: in the tree, or among what
// this server wrote.
export function inside (file)
{
    const at = path.resolve(REPO, file);
    const within = (root) =>
    {
        const rel = path.relative(root, at);

        return rel !== '..' && !rel.startsWith('..' + path.sep) && !path.isAbsolute(rel);
    };

    if (!within(REPO) && !within(OUT))
        throw new Error(`${file} is outside the tree and ${OUT}`);

    return at;
}

// Text or a path: a piece or graph handed over as text is written where a
// harness can read it; a path is used as it is.
export async function source ({ text, path: file }, dir, name)
{
    if (file)
        return inside(file);

    if (typeof text !== 'string')
        throw new Error('give the text, or a path to the file');

    const at = path.join(dir, name);

    await writeFile(at, text);

    return at;
}
