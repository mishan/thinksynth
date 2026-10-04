#!/usr/bin/env python3
"""Sample packs: recorded instruments, built once and published as a release.

The recordings are not in this repository. Every pack is public domain
(CC0) and is built from its source, pinned to a commit: listed with the
GitHub API, downloaded, trimmed, turned to mono 16-bit at 44.1 kHz and
levelled, and named by MIDI note -- the sources disagree about which octave
"C3" is, and a number cannot. That happens once, in the `sample packs'
workflow (.github/workflows/packs.yml), which publishes the result as the
release RELEASE names below; everything else downloads that.

    packs.py build OUTDIR [--cache DIR] [PACK...]
        From the sources: OUTDIR/<pack>/*.flac, OUTDIR/<pack>/pack.json and
        OUTDIR/index.json. Each file is listed under the .wav name its
        instrument plays and the .flac it is served as; the page decodes it.

    packs.py archive OUTDIR
        One OUTDIR/<pack>.tar per built pack, the release's assets.

    packs.py fetch OUTDIR [--missing-ok]
        RELEASE, unpacked into OUTDIR -- what the deploy puts in the site's
        packs/. With --missing-ok a release not published yet is a warning.

    packs.py install [PACK...]
        RELEASE into this user's data directory (thUtil::userDataDir), under
        dsp/samples/, as .wav, where the desktop app looks.

    packs.py zones PACK
        Prints the `file' lines the pack's .dsp uses.

    packs.py tag
        Prints RELEASE.

build and install need ffmpeg. GITHUB_TOKEN, if set, is used for build's
directory listings.
"""

import argparse
import array
import hashlib
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import wave

VSCO = ("sgossner/VSCO-2-CE", "440300901dfe9275fd84e0b7763af1f8443ae62e",
        "VSCO 2 Community Edition, Versilian Studios / Sam Gossner")
RUSTY = ("sfzinstruments/karoryfer.big-rusty-drums",
         "f07ce00df34a46b6b08375be56fe116cf15782bc",
         "Big Rusty Drums, Karoryfer Samples")

# The published packs everything downloads. A change to what a pack holds
# is a new number: a release, once made, is not replaced.
REPO = "mishan/thinksynth"
RELEASE = "packs-1"

RATE = 44100
PEAK = 0.89

NOTE = r"(?P<note>[A-G](?:#|b)?-?\d)"

# A pack: where its files are, how a name gives the note and the velocity
# layer, which layers to keep (as layer 1, 2, 3), how long a file is kept,
# and whether the end is a sustain the .dsp loops.
#
# `octave' is what to add to a name's octave for MIDI: VSCO's orchestral
# folders call middle C "C3", its Upright Nr1 calls it "C4". Each was
# checked against the source's own SFZ `pitch_keycenter'.
PACKS = {
    "violins": dict(
        title="Violin section", source=VSCO,
        dirs=["Strings/Violin Section/susVib"],
        match=r"VlnEns_susVib_" + NOTE + r"_v(?P<layer>\d)\.wav",
        octave=1, layers=["1", "2"], seconds=3.0, sustain=True),
    "cellos": dict(
        title="Cello section", source=VSCO,
        dirs=["Strings/Cello Section/susvib"],
        match=r"susvib_" + NOTE + r"_v(?P<layer>\d)_1\.wav",
        octave=1, layers=["1", "3"], seconds=3.0, sustain=True),
    "horn": dict(
        title="French horn", source=VSCO,
        dirs=["Brass/F Horn/sus"],
        match=r"MOHorn_sus_" + NOTE + r"_v(?P<layer>\d)_1\.wav",
        octave=1, layers=["2", "3", "4"], seconds=2.5, sustain=True),
    "trumpet": dict(
        title="Trumpet", source=VSCO,
        dirs=["Brass/Trumpet/sus"],
        match=r"Sum_SHTrumpet_sus_" + NOTE + r"_v(?P<layer>\d)_rr1\.wav",
        octave=1, layers=["1", "3"], seconds=2.5, sustain=True),
    "flute": dict(
        title="Flute", source=VSCO,
        dirs=["Woodwinds/Flute/susNV"],
        match=r"LDFlute_susNV_" + NOTE + r"_v(?P<layer>\d)_1\.wav",
        octave=1, layers=["1", "3"], seconds=2.5, sustain=True),
    "upright": dict(
        title="Upright piano", source=VSCO,
        dirs=["Keys/Upright Nr1"],
        match=r"UR1_" + NOTE + r"_(?P<layer>pp|mf|f)_RR1\.wav",
        octave=0, layers=["pp", "mf", "f"], seconds=5.0, sustain=False),
}

# The kit: each drum's folder, the General MIDI note it answers, how long
# it rings, and the velocity layers to keep, low to high.
DRUMS = [
    ("kick_24/kick/oh", "k", 36, 1.0, ["4", "9", "14"]),
    ("snare_14/center/oh", "sn_center", 38, 1.2, ["3", "6", "10"]),
    ("tom_22/center/oh", "t22", 41, 1.8, ["2", "4", "7"]),
    ("hihat_14/cl/oh", "ht_cl", 42, 0.6, ["2", "4", "6"]),
    ("tom_18/center/oh", "t18", 43, 1.6, None),
    ("tom_15/center/oh", "t15", 45, 1.5, None),
    ("hihat_14/open/oh", "ht_open", 46, 2.5, ["2", "4", "6"]),
    ("tom_14/center/oh", "t14", 48, 1.4, ["2", "4", "6"]),
    ("crash_17/cr/oh", "cr", 49, 4.0, ["2", "4", "5"]),
    ("ride_22/rd/oh", "rd", 51, 3.0, ["3", "6", "10"]),
]

PACKS["drums"] = dict(title="Drum kit", source=RUSTY, drums=DRUMS,
                      sustain=False)


def midi(name, octave):
    m = re.fullmatch(r"([A-G])(#|b)?(-?\d)", name)
    pc = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[m.group(1)]
    pc += {"#": 1, "b": -1, None: 0}[m.group(2)]
    return 12 * (int(m.group(3)) + 1 + octave) + pc


def listing(repo, sha, path, cache):
    """The file names in a directory at a commit, which never change, so
    they are kept beside the downloads: the API allows sixty requests an
    hour without a token."""
    local = os.path.join(cache, repo.replace("/", "_"), sha[:12],
                         path, ".listing.json")
    if os.path.exists(local):
        with open(local) as f:
            return json.load(f)
    url = (f"https://api.github.com/repos/{repo}/contents/"
           f"{urllib.parse.quote(path)}?ref={sha}")
    req = urllib.request.Request(url)
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req) as r:
        names = [e["name"] for e in json.load(r) if e["type"] == "file"]
    os.makedirs(os.path.dirname(local), exist_ok=True)
    with open(local, "w") as f:
        json.dump(names, f)
    return names


def download(repo, sha, path, cache):
    local = os.path.join(cache, repo.replace("/", "_"), sha[:12], path)
    if not os.path.exists(local):
        os.makedirs(os.path.dirname(local), exist_ok=True)
        url = (f"https://raw.githubusercontent.com/{repo}/{sha}/"
               f"{urllib.parse.quote(path)}")
        with urllib.request.urlopen(url) as r, open(local + ".part", "wb") as f:
            f.write(r.read())
        os.replace(local + ".part", local)
    return local


def decode(src, seconds, sustain):
    """The file as mono 16-bit samples at RATE, its leading silence cut."""
    filt = ("silenceremove=start_periods=1:start_threshold=-60dB,"
            f"atrim=0:{seconds}")
    if not sustain:
        filt += f",afade=t=out:st={max(seconds - 0.08, 0)}:d=0.08"
    out = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", src, "-af", filt, "-ac", "1",
         "-ar", str(RATE), "-f", "s16le", "-"],
        check=True, capture_output=True).stdout
    return array.array("h", out)


def plan(pack, cache):
    """[(source path, output name, MIDI note, layer 1-3)] for a pack."""
    spec = PACKS[pack]
    repo, sha, _ = spec["source"]
    jobs = []
    if "drums" in spec:
        for folder, prefix, note, seconds, layers in spec["drums"]:
            names = listing(repo, sha, f"Samples/{folder}", cache)
            have = sorted({re.fullmatch(prefix + r"_vl(\d+)_rr1\.flac", n)
                           .group(1) for n in names
                           if re.fullmatch(prefix + r"_vl(\d+)_rr1\.flac", n)},
                          key=int)
            if layers is None:
                n = len(have)
                layers = [have[max(0, round(n / 3) - 1)],
                          have[max(0, round(2 * n / 3) - 1)], have[-1]]
            for k, vl in enumerate(layers, 1):
                if vl not in have:
                    sys.exit(f"{folder}: no layer vl{vl}; it has {have}")
                jobs.append((f"Samples/{folder}/{prefix}_vl{vl}_rr1.flac",
                             f"{pack}/{prefix}_{note}_{k}.wav", note, k,
                             seconds))
        return jobs
    for d in spec["dirs"]:
        for n in sorted(listing(repo, sha, d, cache)):
            m = re.fullmatch(spec["match"], n)
            if not m or m.group("layer") not in spec["layers"]:
                continue
            note = midi(m.group("note"), spec["octave"])
            k = spec["layers"].index(m.group("layer")) + 1
            jobs.append((f"{d}/{n}", f"{pack}/{pack}_{note}_{k}.wav", note,
                         k, spec["seconds"]))
    return jobs


def zones(jobs, layer):
    """A `file' line: each note's file at this layer, or at the nearest
    layer the source has for that note -- under it first."""
    have = {}
    for _, out, note, k, _ in jobs:
        have.setdefault(note, {})[k] = out
    picks = []
    for note in sorted(have):
        ks = sorted(have[note], key=lambda k: (k > layer, abs(k - layer)))
        picks.append(f"{have[note][ks[0]]}@{note}")
    return " ".join(picks)


def build(pack, outdir, cache, flac):
    spec = PACKS[pack]
    repo, sha, credit = spec["source"]
    jobs = plan(pack, cache)
    decoded = []
    for path, out, note, k, seconds in jobs:
        decoded.append((out, decode(download(repo, sha, path, cache), seconds,
                                    spec["sustain"])))
    # One gain for the whole pack, so the balance across the range and
    # between layers is the recording's.
    peak = max(max(abs(min(a)), max(a)) for _, a in decoded if len(a)) or 1
    gain = PEAK * 32767 / peak
    files = []
    for out, a in decoded:
        scaled = array.array("h", (max(-32768, min(32767, round(x * gain)))
                                   for x in a))
        path = os.path.join(outdir, out)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if flac:
            # The name stays the .wav the instrument plays; what is served
            # is FLAC, about half the size, which the page decodes.
            served = out[:-4] + ".flac"
            subprocess.run(
                ["ffmpeg", "-v", "error", "-y", "-f", "s16le", "-ar",
                 str(RATE), "-ac", "1", "-i", "-", "-compression_level", "8",
                 os.path.join(outdir, served)],
                input=scaled.tobytes(), check=True)
            files.append({"name": out, "url": served, "bytes":
                          os.path.getsize(os.path.join(outdir, served))})
        else:
            with wave.open(path, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(RATE)
                w.writeframes(scaled.tobytes())
            files.append({"name": out, "bytes": os.path.getsize(path)})
    # What the page compares to tell a rebuilt pack from the one it kept.
    digest = hashlib.sha256()
    for f in files:
        with open(os.path.join(outdir, f.get("url", f["name"])), "rb") as h:
            digest.update(f["name"].encode() + b"\0" + h.read())
    manifest = {
        "id": pack, "title": spec["title"], "license": "CC0-1.0",
        "version": digest.hexdigest()[:16],
        "credit": credit, "source": f"https://github.com/{repo}/tree/{sha}",
        "bytes": sum(f["bytes"] for f in files), "files": files,
    }
    with open(os.path.join(outdir, pack, "pack.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    return manifest


def user_samples():
    if sys.platform == "win32":
        base = os.path.join(os.environ["LOCALAPPDATA"], "thinksynth")
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support/thinksynth")
    else:
        xdg = os.environ.get("XDG_DATA_HOME", "")
        base = os.path.join(xdg if xdg.startswith("/")
                            else os.path.expanduser("~/.local/share"),
                            "thinksynth")
    return os.path.join(base, "dsp", "samples")


def asset(name):
    return f"https://github.com/{REPO}/releases/download/{RELEASE}/{name}"


def archive(outdir):
    with open(os.path.join(outdir, "index.json")) as f:
        index = json.load(f)
    for p in index:
        with tarfile.open(os.path.join(outdir, p["id"] + ".tar"), "w") as t:
            t.add(os.path.join(outdir, p["id"]), arcname=p["id"])


def inside(root, name):
    """root/name, if it stays under root; None if it would leave it."""
    path = os.path.realpath(os.path.join(root, name))
    top = os.path.realpath(root)
    return path if os.path.commonpath([path, top]) == top else None


def extract(t, outdir):
    """A tar's files into outdir and nowhere else. tarfile's own `data'
    filter where this Python has it (3.12, and backported to 3.8.17,
    3.9.17, 3.10.12, 3.11.4); otherwise the same refusal by hand, since
    the python3 macOS ships is 3.9.6."""
    if hasattr(tarfile, "data_filter"):
        t.extractall(outdir, filter="data")
        return
    for m in t.getmembers():
        if not (m.isfile() or m.isdir()) or inside(outdir, m.name) is None:
            sys.exit(f"refusing {m.name} in a pack")
    t.extractall(outdir)


def fetch(outdir, missing_ok=False, names=()):
    """RELEASE's packs into outdir -- those named, or all of them; the
    index, or None if there is none."""
    try:
        with urllib.request.urlopen(asset("index.json")) as r:
            index = json.load(r)
    except urllib.error.HTTPError as e:
        if e.code != 404:
            raise
        if missing_ok:
            print(f"::warning::{RELEASE} is not published; no sample packs")
            return None
        sys.exit(f"{RELEASE} is not published yet: run the `sample packs' "
                 "workflow, or build from the sources with `packs.py build'")
    index = [p for p in index if not names or p["id"] in names]
    os.makedirs(outdir, exist_ok=True)
    for p in index:
        with urllib.request.urlopen(asset(p["id"] + ".tar")) as r, \
                tempfile.TemporaryFile() as tmp:
            tmp.write(r.read())
            tmp.seek(0)
            with tarfile.open(fileobj=tmp) as t:
                extract(t, outdir)
        print(f"{p['id']}: {p['bytes'] / 1e6:.1f} MB")
    with open(os.path.join(outdir, "index.json"), "w") as f:
        json.dump(index, f, indent=1)
    return index


def install(names):
    """RELEASE, decoded to .wav, into the user's samples directory."""
    dest = user_samples()
    with tempfile.TemporaryDirectory() as tmp:
        for p in fetch(tmp, names=names):
            with open(os.path.join(tmp, p["id"], "pack.json")) as f:
                m = json.load(f)
            for f in m["files"]:
                src = inside(tmp, f.get("url", f["name"]))
                out = inside(dest, f["name"])
                if src is None or out is None:
                    sys.exit(f"refusing {f['name']} in {p['id']}")
                os.makedirs(os.path.dirname(out), exist_ok=True)
                subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src,
                                out], check=True)
            print(f"{p['id']}: installed in {dest}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("mode", choices=["build", "archive", "fetch", "install",
                                     "zones", "tag"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--cache", default=os.path.join(tempfile.gettempdir(),
                                                    "thinksynth-packs"))
    ap.add_argument("--missing-ok", action="store_true")
    opt = ap.parse_intermixed_args()

    if opt.mode == "tag":
        print(RELEASE)
        return
    if opt.mode in ("archive", "fetch"):
        if not opt.args:
            ap.error(f"{opt.mode} needs an output directory")
        if opt.mode == "archive":
            archive(opt.args[0])
        else:
            fetch(opt.args[0], opt.missing_ok)
        return
    if opt.mode == "install":
        install(opt.args)
        return

    if opt.mode == "zones":
        for pack in opt.args:
            jobs = plan(pack, opt.cache)
            top = max(k for *_, k, _ in jobs)
            for layer in range(1, top + 1):
                print(f'file{"" if layer == 1 else layer} = "'
                      f'{zones(jobs, layer)}";')
        return

    if not opt.args:
        ap.error("build needs an output directory")
    outdir, names = opt.args[0], opt.args[1:] or list(PACKS)

    index = []
    for pack in names:
        m = build(pack, outdir, opt.cache, True)
        index.append({k: m[k] for k in ("id", "title", "license", "credit",
                                         "version", "bytes")}
                     | {"files": len(m["files"])})
        print(f"{pack}: {len(m['files'])} files, {m['bytes'] / 1e6:.1f} MB")
    with open(os.path.join(outdir, "index.json"), "w") as f:
        json.dump(index, f, indent=1)


if __name__ == "__main__":
    main()
