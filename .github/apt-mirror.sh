#!/bin/sh
#
# Points apt at Azure's Ubuntu mirror before a CI job installs anything.
#
# A runner reads its mirrors from /etc/apt/apt-mirrors.txt, and some come up
# with a regional EC2 mirror at the top of it. That mirror has been taking
# seconds over each small package, so a job that drew it ran out its install
# step's ten minutes; the same packages from azure.archive.ubuntu.com took
# fifteen seconds in the same run. apt retries a fetch that fails, too.
set -e

for f in /etc/apt/apt-mirrors.txt /etc/apt/sources.list.d/ubuntu.sources; do
    if [ -f "$f" ]; then
        sed -Ei 's#https?://[a-z0-9-]+\.ec2\.archive\.ubuntu\.com/#http://azure.archive.ubuntu.com/#g' "$f"
    fi
done

echo 'Acquire::Retries "3";' > /etc/apt/apt.conf.d/80retries
