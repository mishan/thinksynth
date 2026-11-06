#!/bin/sh
# Installs the udev rule that lets whoever is at the seat read a wired Valve
# Index controller, and applies it to one already plugged in.
set -e

rule="$(dirname "$0")/70-thinksynth-index.rules"

if [ "$(id -u)" -ne 0 ]; then
    exec sudo "$0" "$@"
fi

install -m 0644 "$rule" /etc/udev/rules.d/
udevadm control --reload-rules
udevadm trigger --subsystem-match=hidraw --action=change

echo "Installed /etc/udev/rules.d/$(basename "$rule")."
echo "If thinksynth still can't open the controller, unplug it and plug it in again."
