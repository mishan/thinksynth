#!/usr/bin/env python3
"""The engine's cost in instructions, held to a committed baseline.

    instrcount.py --toolchain KEY [--update] BUILD_DIR

Runs scripts/dspbench on a few instruments under Valgrind's cachegrind and
counts the instructions each run executes. A count is the same to within
about a part in a million from one run to the next, and a few parts in ten
thousand from one machine to another, where a wall-clock time on the same
machine moves by several percent, so a count can fail a change that a time
could only hint at.
Valgrind and not `perf stat': a hosted CI runner exposes no hardware
counters at all, and a count from one would depend on which CPU the job
landed on, through the library routines glibc picks at load.

A count is a property of the compiler, its flags and the C library as much
as of the code, so a set of counts is held to the toolchain it was measured
with, named by KEY -- which CMake passes as the compiler, its version, the
build type, the C++ flags and whether LTO is on -- with the glibc and
Valgrind versions added here.

The baseline committed beside this, instrcount.json, holds CI's toolchain
alone. A desktop's is its owner's: it would go stale with every upgrade of
their system, and every change that moves a count would owe it numbers
from a machine its author does not have. A desktop's counts are held in
BUILD_DIR/instrcount.json instead, which no commit carries, for comparing
a change against the tree before it. A toolchain with counts in neither is
skipped (exit 77, which ctest reports as a skip), unless
THINK_INSTRCOUNT_STRICT is set, as CI sets it, so that the gate cannot
quietly stop running when the runner's compiler changes; it fails and
prints what it measured instead.

--update measures this toolchain's counts and writes them where they are
held: over its own in instrcount.json, leaving any other there alone, and
otherwise as the whole of BUILD_DIR/instrcount.json, so that file never
collects stale toolchains. `cmake --build BUILD_DIR --target
instrcount-update' runs it with the build's KEY. Do that when a change is
meant to cost more, and when it costs less by more than FLOOR, which fails
too, in the same commit, so the reason is reviewed with the number. A
failure prints the counts it measured, for CI's toolchain from its log.
CI's counts come from the Ubuntu 24.04 image the linux job pins: the commit
that moves the pin replaces the key in instrcount.json with the new image's,
from a strict failure's log or a container of that image.
"""

import argparse
import concurrent.futures
import json
import os
import platform
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
BASELINE = os.path.join(HERE, 'instrcount.json')

# A count above the baseline by more than this fails. The noise is under a
# thousandth of a percent, so this is not a margin for it: it is how much a
# change may cost before it has to say so with --update.
LIMIT = 1.10

# A count below the baseline by more than this fails too, so that a change
# that makes the engine cheaper lowers the baseline with --update in its
# own commit. Without it the room a speedup made is spent unseen by later
# changes under LIMIT, the one that crosses LIMIT takes the blame for all of
# them, and a regression of up to LIMIT / FLOOR passes. Far from the noise:
# a count repeats to about a part in a million on one machine, and a
# runner's came within a few parts in ten thousand of a container's on a
# desktop with another CPU.
FLOOR = 0.98

# dspbench's arguments for each instrument: a delay-line comb, a piano's
# waveguides, an FM voice and a modeled drum, at a few voices and a few
# dozen windows each. Sized so that the whole set is seconds under Valgrind;
# a startup's fixed cost is in every count, and that is a cost too.
WORKLOADS = {
    'fircomb': ['-n', '4', '-w', '10'],
    'grand': ['-n', '8', '-w', '20'],
    'dx5': ['-n', '8', '-w', '16'],
    'kit_snare': ['-n', '8', '-w', '40'],
}


def toolchain(key):
    valgrind = subprocess.run(['valgrind', '--version'], capture_output=True,
                              text=True, check=True).stdout.strip()
    return f'{key}; glibc {platform.libc_ver()[1]}; {valgrind}'


def count(build, name, args):
    run = subprocess.run(
        ['valgrind', '--tool=cachegrind', '--cache-sim=no',
         '--cachegrind-out-file=/dev/null',
         os.path.join(build, 'scripts', 'dspbench'),
         '-p', os.path.join(build, 'plugins', ''), *args,
         os.path.join(TOP, 'dsp', name + '.dsp')],
        capture_output=True, text=True,
        env={**os.environ, 'THINK_DSP_PATH': os.path.join(TOP, 'dsp')})
    found = re.search(r'I\s+refs:\s+([\d,]+)', run.stderr)

    if run.returncode != 0 or found is None:
        sys.exit(f'{name}: dspbench under valgrind failed\n{run.stderr}')

    return int(found.group(1).replace(',', ''))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--toolchain', required=True)
    parser.add_argument('--update', action='store_true')
    parser.add_argument('build')
    opts = parser.parse_args()
    strict = os.environ.get('THINK_INSTRCOUNT_STRICT', '') != ''

    if shutil.which('valgrind') is None:
        if strict or opts.update:
            sys.exit('instrcount: no valgrind')

        print('instrcount: no valgrind; skipped')
        return 77

    key = toolchain(opts.toolchain)

    local = os.path.join(opts.build, 'instrcount.json')
    held = BASELINE

    with open(BASELINE) as f:
        baseline = json.load(f)

    if key not in baseline:
        held, baseline = local, {}

        if os.path.exists(local):
            with open(local) as f:
                baseline = json.load(f)

    known = baseline.get(key)

    if known is None and not strict and not opts.update:
        print(f'instrcount: no counts for "{key}" in {BASELINE} or {local};'
              f' skipped. `cmake --build {opts.build} --target'
              " instrcount-update' records them.")
        return 77

    # One workload a process, and they are independent, so at once.
    with concurrent.futures.ThreadPoolExecutor() as pool:
        jobs = {name: pool.submit(count, opts.build, name, args)
                for name, args in WORKLOADS.items()}
        measured = {f'{name} {" ".join(WORKLOADS[name])}': job.result()
                    for name, job in jobs.items()}

    if opts.update:
        if held == local:
            baseline = {}

        baseline[key] = measured

        with open(held, 'w') as f:
            json.dump(baseline, f, indent=2, sort_keys=True)
            f.write('\n')

        print(f'instrcount: wrote the counts for "{key}" to {held}')
        return 0

    if known is None:
        print(f'instrcount: no counts for "{key}" in {BASELINE}. '
              'Measured, for the file:\n' +
              json.dumps({key: measured}, indent=2, sort_keys=True))
        return 1

    failed = 0
    table = [f'toolchain: {key}',
             f'{"workload":<26} {"baseline":>15} {"measured":>15}  ratio']

    for name, got in measured.items():
        want = known.get(name)

        if want is None:
            table.append(f'{name:<26} {"none":>15} {got:>15,}  -- FAIL, '
                         'not in the baseline')
            failed += 1
            continue

        ratio = got / want
        note = ''

        if ratio > LIMIT:
            note = f'  FAIL, over {LIMIT:.2f}'
            failed += 1
        elif ratio < FLOOR:
            note = f'  FAIL, under {FLOOR:.2f}: --update lowers the baseline'
            failed += 1

        table.append(f'{name:<26} {want:>15,} {got:>15,}  {ratio:.4f}{note}')

    if failed:
        table.append('Measured, for the file:\n' +
                     json.dumps({key: measured}, indent=2, sort_keys=True))

    # Beside the build as well, because ctest shows a passing test's output
    # to nobody and CI prints this file to show the margin.
    print('\n'.join(table))

    with open(os.path.join(opts.build, 'instrcount.txt'), 'w') as f:
        f.write('\n'.join(table) + '\n')

    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
