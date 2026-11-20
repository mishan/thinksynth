# The images CI's Linux jobs that build the desktop run in: every package
# they used to install from a mirror at the start of each run, installed
# once. Two: `headless', with nothing but what the build without the GUI
# asks for, so a GTK include creeping into that half still fails for want
# of one; and the full one on top of it. And a third, `browsers', for the
# jobs that drive the page: Playwright's own image, whose browsers are the
# version wasm/web/package.json pins (the image job checks), with an audio
# server for the live-context tests and the libraries the desktop's tools
# those jobs run need.
#
# Built and published by ci.yml's `image' job, tagged by a hash of this
# file, so changing it builds a new one, and a pull request that changes it
# runs on the image it describes. The mirrors were the slowest and least
# reliable part of a run: a crawling one ran jobs out of time before they
# built anything.
#
# Beyond what the build asks for: git for actions/checkout, sudo so a step
# written for the runner's own image runs as it is, ccache for
# ccache-action, jq for the site's config.json, python3 and xz-utils for
# emsdk, and the comma-decimal
# locale localecheck needs. And xvfb-run's screen made 2560x1440: the
# plugin editor at 2x is 1768 wide (guicheck), and a window past the
# screen's edge can't be read back.

FROM ubuntu:24.04 AS headless

LABEL org.opencontainers.image.source=https://github.com/mishan/thinksynth
LABEL org.opencontainers.image.description="thinksynth's Linux CI"

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -Ei 's#http://archive\.ubuntu\.com#http://azure.archive.ubuntu.com#' \
        /etc/apt/sources.list.d/ubuntu.sources \
 && echo 'Acquire::Retries "3";' > /etc/apt/apt.conf.d/80retries \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build flex pkg-config \
        libsigc++-3.0-dev zlib1g-dev git ca-certificates \
 && rm -rf /var/lib/apt/lists/*

FROM mcr.microsoft.com/playwright:v1.63.0-noble AS browsers

LABEL org.opencontainers.image.source=https://github.com/mishan/thinksynth
LABEL org.opencontainers.image.description="thinksynth's CI in browsers"

ENV DEBIAN_FRONTEND=noninteractive

RUN echo 'Acquire::Retries "3";' > /etc/apt/apt.conf.d/80retries \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
        pulseaudio libgtkmm-4.0-dev libsigc++-3.0-dev \
 && rm -rf /var/lib/apt/lists/*

FROM headless

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        libgtkmm-4.0-dev libjpeg-dev \
        libasound2-dev libjack-jackd2-dev \
        adwaita-icon-theme librsvg2-common \
        libx11-dev libxcursor-dev libxext-dev libxrandr-dev \
        libxtst-dev libdbus-1-dev \
        valgrind xvfb xauth locales \
        sudo ccache curl jq python3 xz-utils \
 && locale-gen de_DE.UTF-8 && locale -a | grep -qix de_DE.utf8 \
 && sed -i 's/^XVFBARGS="-screen 0 1280x1024x24"$/XVFBARGS="-screen 0 2560x1440x24"/' \
        /usr/bin/xvfb-run \
 && grep -q 2560x1440x24 /usr/bin/xvfb-run \
 && rm -rf /var/lib/apt/lists/*
