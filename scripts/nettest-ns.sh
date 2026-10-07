#!/bin/sh
#
# Copyright (C) 2004-2026 The thinksynth authors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.

# nettest-ns.sh -- a private network of N hosts for wasm/web/nettest.mjs,
# with no privilege: a user and network namespace of its own (the hub),
# and in it one more network namespace per host.
#
#   scripts/nettest-ns.sh N command [arg...]
#
#        hub: br0 10.77.0.1/24 -- the relay and the harness
#         |
#     +---+--------+--- ...
#    nt1          nt2            veth, hub side: netem here is the host's
#     |            |               downlink
#    eth0         eth0           veth, host side: netem here is its uplink
#  10.77.0.11   10.77.0.12       host i, a namespace held open by a sleep
#
# So each host has a link of its own, and a packet between two hosts
# crosses both: the sender's uplink and the receiver's downlink, as two
# players at home do. The relay sits on the bridge with no link of its own.
#
# The command runs in the hub with, in its environment:
#
#   NETTEST_HUB     the hub's address, 10.77.0.1
#   NETTEST_PIDS    a process in each host's namespace, in host order:
#                   `nsenter -t PID -n' runs something there, which is how
#                   a browser is started on a host and how its uplink's
#                   qdisc is changed
#
# The namespaces have no route out, which is the point: WebRTC finds
# nothing but host candidates, and the mesh has to open on those. Each
# host has a default route to the hub all the same, because Chromium
# gathers candidates only on an interface that has one.
#
# Everything goes when the command exits, however it ends: this script is
# the first process of a PID namespace of its own, and the kernel kills
# everything else in it when it goes, browsers and all, and itself if
# unshare is killed. A namespace with no process in it is gone.

set -eu

if [ $# -lt 2 ]; then
    echo "usage: $0 N command [arg...]" >&2
    exit 2
fi

PATH="$PATH:/usr/sbin:/sbin"

if [ -z "${NETTEST_IN_HUB:-}" ]; then
    NETTEST_IN_HUB=1 exec unshare -rnmpf --mount-proc --kill-child "$0" "$@"
fi

n=$1
shift

holders=

ip link set lo up
ip link add br0 type bridge
ip addr add 10.77.0.1/24 dev br0
ip link set br0 up

hub=$(readlink /proc/self/ns/net)
i=1
while [ "$i" -le "$n" ]; do
    unshare -n sleep infinity &
    pid=$!
    holders="$holders $pid"

    # The holder has its namespace once unshare has made it, which is a
    # moment after the fork.
    tries=0
    while [ "$(readlink "/proc/$pid/ns/net")" = "$hub" ]; do
        tries=$((tries + 1))
        if [ "$tries" -gt 200 ]; then
            echo "$0: host $i got no namespace" >&2
            exit 1
        fi
        sleep 0.01
    done

    ip link add "nt$i" type veth peer name eth0 netns "$pid"
    ip link set "nt$i" master br0 up
    nsenter -t "$pid" -n sh -c "
        ip link set lo up
        ip addr add 10.77.0.$((10 + i))/24 dev eth0
        ip link set eth0 up
        ip route add default via 10.77.0.1"
    i=$((i + 1))
done

export NETTEST_HUB=10.77.0.1
export NETTEST_PIDS="${holders# }"

# Not exec: the first process of a PID namespace ignores a signal it has
# no handler for, and the command is to die of a ^C.
"$@"
