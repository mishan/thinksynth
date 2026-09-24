#!/bin/sh
#
# Copyright (C) 2004-2026 Metaphonic Labs
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.

# headless.sh -- runs a command on a private X server and a private, silent
# PipeWire, so a test neither opens a window on the desktop nor plays
# through the speakers.
#
#   scripts/headless.sh ctest --test-dir build -j "$(nproc)"
#   scripts/headless.sh node wasm/web/pagetest.mjs build-web
#
# ctest does this by itself on Linux (THINK_TEST_HEADLESS in the top-level
# CMakeLists.txt); calling it by hand is for the web tests, and for anything
# else that opens a window or an AudioContext.
#
# What the command gets:
#
#   an Xvfb display, with WAYLAND_DISPLAY unset and GDK_BACKEND=x11 -- with
#     a Wayland socket to find, GTK ignores DISPLAY altogether
#   a PipeWire daemon of its own, speaking the pulse protocol too, whose one
#     sink is a null sink. No ALSA or Bluetooth device is ever opened: the
#     daemon loads no device monitor and its WirePlumber runs none, so there
#     is nothing to route a stream to but the null sink. That sink is still
#     clocked, by the dummy driver, so a player or an AudioContext runs at
#     the speed it would on hardware.
#   its own XDG_RUNTIME_DIR, which is where every audio client looks for
#     its server, and PULSE_SERVER, for one configured to look elsewhere
#   its own D-Bus session bus, on which no service can be activated
#
# Whatever is missing is left out, loudly, rather than failing the run: no
# Xvfb means the desktop's display is hidden instead (a GUI check then
# skips itself), and no PipeWire means no audio server at all, so a client
# that wants one fails rather than finding the real one.
#
# Nested calls pass straight through, so a ctest run under this does not
# start a second set of servers per test.

set -eu

if [ $# -eq 0 ]; then
    echo "usage: $0 command [arg...]" >&2
    exit 2
fi

# Already inside one: nothing to add.
if [ -n "${THINK_HEADLESS:-}" ]; then
    exec "$@"
fi

dir=$(mktemp -d "${TMPDIR:-/tmp}/thinksynth-headless.XXXXXX")
xpid=
pwpid=
wppid=
buspid=

# Unchecked: under -e, the nonzero status wait gives back for a server it
# just killed would end the trap there and leave the rest running -- and
# the one still holding ctest's output open would stall the whole run.
cleanup()
{
    set +e
    for pid in $buspid $wppid $pwpid $xpid; do
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    done
    rm -rf "$dir"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -m 700 "$dir/run"

unset WAYLAND_DISPLAY DISPLAY PIPEWIRE_REMOTE PULSE_SERVER
export XDG_RUNTIME_DIR="$dir/run"
export PIPEWIRE_RUNTIME_DIR="$dir/run"
export GDK_BACKEND=x11
export THINK_HEADLESS=1

# Waits up to five seconds for a file to be non-empty (Xvfb's display
# number) or to exist (a socket).
await()
{
    i=0
    while [ ! "$1" "$2" ]; do
        i=$((i + 1))
        [ "$i" -gt 100 ] && return 1
        sleep 0.05
    done
}

if command -v Xvfb >/dev/null 2>&1; then
    # -displayfd: Xvfb picks a free display itself and writes its number,
    # which is what makes parallel runs safe without xvfb-run's retry loop.
    Xvfb -displayfd 3 -nolisten tcp -screen 0 1920x1080x24 \
        3>"$dir/display" >"$dir/xvfb.log" 2>&1 &
    xpid=$!
    if await -s "$dir/display"; then
        export DISPLAY=":$(cat "$dir/display")"
    else
        echo "headless.sh: Xvfb did not start:" >&2
        cat "$dir/xvfb.log" >&2
        exit 1
    fi
else
    echo "headless.sh: no Xvfb; running with no display" >&2
fi

if command -v pipewire >/dev/null 2>&1; then
    # The whole config, not a fragment on the system one: that one loads the
    # ALSA monitor, and a user's pipewire.conf.d is named for it too.
    cat >"$dir/headless-pipewire.conf" <<'EOF'
context.properties = {
    core.daemon = true
    core.name   = pipewire-0
    support.dbus = false
}
context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.*       = support/libspa-support
}
context.modules = [
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-metadata }
    { name = libpipewire-module-spa-node-factory }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-access }
    { name = libpipewire-module-adapter }
    { name = libpipewire-module-link-factory }
    { name = libpipewire-module-protocol-pulse }
]
pulse.properties = {
    server.address = [ "unix:native" ]
}
context.objects = [
    { factory = metadata
        args = {
            metadata.name = default
            metadata.values = [
                { key = default.audio.sink   value = { name = headless-sink } }
                { key = default.audio.source value = { name = headless-sink } }
            ]
        }
    }
    { factory = spa-node-factory
        args = {
            factory.name    = support.node.driver
            node.name       = Dummy-Driver
            node.group      = pipewire.dummy
            priority.driver = 20000
        }
    }
    { factory = spa-node-factory
        args = {
            factory.name    = support.node.driver
            node.name       = Freewheel-Driver
            priority.driver = 19000
            node.group      = pipewire.freewheel
            node.freewheel  = true
        }
    }
    { factory = adapter
        args = {
            factory.name     = support.null-audio-sink
            node.name        = headless-sink
            node.description = "Headless test sink"
            media.class      = Audio/Sink
            audio.position   = [ FL FR ]
            object.linger    = true
        }
    }
]
EOF
    PIPEWIRE_CONFIG_DIR="$dir" pipewire -c "$dir/headless-pipewire.conf" \
        >"$dir/pipewire.log" 2>&1 &
    pwpid=$!
    if await -S "$dir/run/pulse/native"; then
        export PULSE_SERVER="unix:$dir/run/pulse/native"
    else
        echo "headless.sh: PipeWire did not start:" >&2
        cat "$dir/pipewire.log" >&2
        exit 1
    fi

    # WirePlumber only for linking: a stream nothing links is never driven,
    # and a player waiting on it waits forever. "policy" is the profile with
    # no hardware monitors in it. Stateless, off D-Bus, and on a config and
    # state home of its own, so it neither reads the user's WirePlumber
    # config nor writes this sink into their saved defaults.
    if command -v wireplumber >/dev/null 2>&1; then
        mkdir -p "$dir/config/wireplumber/wireplumber.conf.d"
        cat >"$dir/config/wireplumber/wireplumber.conf.d/headless.conf" <<'EOF'
wireplumber.profiles = {
    headless = {
        inherits = [ policy, mixin.systemwide-session, mixin.stateless ]
        support.dbus = disabled
        support.mpris = disabled
    }
}
EOF
        env -u DBUS_SESSION_BUS_ADDRESS \
            XDG_CONFIG_HOME="$dir/config" XDG_STATE_HOME="$dir/state" \
            wireplumber --profile headless >"$dir/wireplumber.log" 2>&1 &
        wppid=$!
    else
        echo "headless.sh: no WirePlumber; audio streams will not run" >&2
    fi
else
    echo "headless.sh: no PipeWire; running with no audio server" >&2
    # Somewhere that answers nothing, so no client falls back to the
    # desktop's own through a client.conf.
    export PULSE_SERVER="unix:$dir/run/none"
fi

# A bus with nothing to activate, not dbus-run-session: that one reads the
# desktop's service files, and the first browser to ask for a portal starts
# xdg-desktop-portal, the keyring and the accessibility bus on it.
if command -v dbus-daemon >/dev/null 2>&1; then
    cat >"$dir/bus.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path=$dir/run/bus</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
    dbus-daemon --nofork --config-file="$dir/bus.conf" >"$dir/dbus.log" 2>&1 &
    buspid=$!
    if await -S "$dir/run/bus"; then
        export DBUS_SESSION_BUS_ADDRESS="unix:path=$dir/run/bus"
    else
        echo "headless.sh: dbus-daemon did not start:" >&2
        cat "$dir/dbus.log" >&2
        exit 1
    fi
else
    unset DBUS_SESSION_BUS_ADDRESS
fi
export NO_AT_BRIDGE=1

# Not exec: the trap has to outlive the command to stop the servers.
"$@" && status=0 || status=$?
exit "$status"
