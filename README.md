thinksynth
==========

thinksynth is a modular software synthesizer written in C++

Building
--------

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
./build/src/thinksynth
```

There is no `./configure`. The autotools build was retired in favour of CMake
during the macOS/Windows port; see `PORTING.md` for why.

On Debian or Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build bison flex pkg-config \
    libgtkmm-3.0-dev libsigc++-2.0-dev libasound2-dev libjack-jackd2-dev
```

Useful options:

| | |
|---|---|
| `-DCMAKE_BUILD_TYPE=Debug` | unoptimised, with symbols |
| `-DTHINK_ENABLE_DEBUG=ON` | the tree's own debug logging |
| `-DTHINK_SANITIZE=address,undefined` | applied to libthink and the plugins too, not just the harnesses |
| `-DTHINK_SANITIZE=thread` | needs its own build tree; cannot be combined with the above |

Running an uninstalled build works: thinksynth looks for its plugins next to
its own binary and in `./plugins`, and `THINK_PLUGIN_PATH` overrides that.
