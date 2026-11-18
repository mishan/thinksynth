#!/usr/bin/env python3
"""listenpagecheck -- scripts/listenpage.py against a two-bar piece.

    listenpagecheck.py LISTENPAGE GENWAV PLUGINS PIECE WORKDIR

Renders PIECE's tape with genwav for what the page should show, runs
listenpage on it, and checks the page: one section, one bar in the roll for
each note the tape has, every instrument in the legend, an MP3 the player
points at, nothing loaded from another site, and a warning naming the
instrument that rendered silent. Exits 77, a skip, without ffmpeg.
"""

import os
import re
import shutil
import subprocess
import sys


def main():
    listenpage, genwav, plugins, piece, work = sys.argv[1:6]
    if shutil.which("ffmpeg") is None:
        print("no ffmpeg; skipping")
        return 77
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    tape = os.path.join(work, "expected.tape")
    subprocess.run([genwav, "-q", "-p", plugins, "-s", "10", "-t", tape,
                    "-o", os.path.join(work, "expected.wav"), piece],
                   check=True)
    with open(tape) as f:
        lines = f.read().splitlines()
    notes = sum(1 for l in lines if l.startswith("N "))
    names = [l.split("= ", 1)[1] for l in lines if l.startswith("# channel")]

    out = os.path.join(work, "site")
    run = subprocess.run([sys.executable, listenpage, "--genwav", genwav,
                          "--plugins", plugins, "-s", "10", "-o", out, piece],
                         capture_output=True, text=True, check=False)
    sys.stderr.write(run.stderr)
    failures = []
    if run.returncode != 0:
        print(f"FAIL: listenpage exited {run.returncode}")
        return 1
    with open(os.path.join(out, "index.html")) as f:
        page = f.read()

    sections = page.count("<section")
    if sections != 1:
        failures.append(f"{sections} sections, wanted 1")
    drawn = len(re.findall(r'<rect [^>]*class="note', page))
    if drawn != notes or notes == 0:
        failures.append(f"{drawn} notes drawn, the tape has {notes}")
    for name in names:
        if f">{name}<" not in page:
            failures.append(f"{name} is not in the legend")
    srcs = re.findall(r'<audio [^>]*src="([^"]+)"', page)
    if len(srcs) != 1:
        failures.append(f"{len(srcs)} players, wanted 1")
    for src in srcs:
        path = os.path.join(out, src)
        if not os.path.isfile(path):
            failures.append(f"{src} does not exist")
            continue
        with open(path, "rb") as f:
            head = f.read(3)
        if os.path.getsize(path) < 1000 or not (
                head == b"ID3" or head[:2] in (b"\xff\xfb", b"\xff\xf3")):
            failures.append(f"{src} is not an MP3")
    remote = re.findall(r'(?:src|href)="(?:https?:)?//', page)
    if remote:
        failures.append(f"{len(remote)} things loaded from another site")
    if not re.search(r"ghost.*silent|silent.*ghost", run.stderr):
        failures.append("no warning that ghost rendered silent")
    for name in names:
        if name != "ghost" and re.search(rf"\b{name}\b.*silent", run.stderr):
            failures.append(f"{name} is called silent")

    for f in failures:
        print(f"FAIL: {f}")
    if not failures:
        print(f"ok: {notes} notes, {len(names)} instruments")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
