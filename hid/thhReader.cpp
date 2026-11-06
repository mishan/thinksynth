/*
 * Copyright (C) 2004-2026 The thinksynth authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include "thhIndex.h"
#include "thhReader.h"

namespace fs = std::filesystem;

static std::string
slurp (const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);

    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

static bool
isIndex (const std::string &uevent)
{
    return uevent.find("\nHID_ID=0003:000028DE:00002300\n") !=
               std::string::npos &&
           uevent.find("\nHID_NAME=Valve Index Controller\n") !=
               std::string::npos;
}

bool
thhFindIndex (const std::string &sysRoot, const std::string &devRoot,
              thhIndexNodes &out)
{
    /* Each interface's HID device sits in an interface directory under its
       USB device's, which is what ties a controller's nodes together. Keyed
       by that path, the first is the same one every time. */
    std::map<fs::path, thhIndexNodes> byUsb;
    std::error_code ec;

    for (fs::directory_iterator it(fs::path(sysRoot) / "class/hidraw", ec), end;
         !ec && it != end; it.increment(ec))
    {
        const fs::path dev = fs::canonical(it->path() / "device", ec);

        if (ec || !isIndex("\n" + slurp(dev / "uevent")))
            continue;

        const std::string node = devRoot + "/" +
                                 it->path().filename().string();
        const std::string desc = slurp(dev / "report_descriptor");
        thhIndexNodes &n = byUsb[dev.parent_path().parent_path()];

        n.all.push_back(node);
        if (thhIndexIsInput((const uint8_t *)desc.data(), desc.size()))
            n.input = node;
    }

    for (auto &u : byUsb)
        if (!u.second.input.empty())
        {
            out = u.second;
            std::sort(out.all.begin(), out.all.end());
            return true;
        }

    return false;
}

bool
thhConnectIndex (const std::string &sysRoot, const std::string &devRoot,
                 int &input, std::vector<int> &fds, std::string &status)
{
    thhIndexNodes n;

    if (!thhFindIndex(sysRoot, devRoot, n))
    {
        status = "no Valve Index controller is plugged in";
        return false;
    }

    std::vector<int> opened;

    for (size_t i = 0; i < n.all.size(); i++)
    {
        const int fd = open(n.all[i].c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        bool woke = fd >= 0;

        for (int k = 0; woke && k < 2; k++)
        {
            uint8_t buf[sizeof(THH_INDEX_INIT[k])];

            memcpy(buf, THH_INDEX_INIT[k], sizeof(buf));
            woke = ioctl(fd, HIDIOCSFEATURE(sizeof(buf)), buf) >= 0;
        }

        if (!woke)
        {
            status = fd < 0 && errno == EACCES
                ? "no permission to open " + n.all[i] + "; run "
                  "hid/install-udev-rule.sh to install the udev rule that "
                  "allows it"
                : (fd < 0 ? "can't open " : "can't wake ") + n.all[i] + ": " +
                  strerror(errno);
            if (fd >= 0)
                close(fd);
            for (size_t k = 0; k < opened.size(); k++)
                close(opened[k]);
            return false;
        }

        opened.push_back(fd);
    }

    for (size_t i = 0; i < n.all.size(); i++)
        if (n.all[i] == n.input)
            input = opened[i];
    fds.swap(opened);
    status.clear();
    return true;
}

thhReader::thhReader (const Connect &connect,
                      const std::function<void ()> &wake, int silenceMs)
    : connect_(connect), wake_(wake), silenceMs_(silenceMs), notified_(false),
      fresh_(false)
{
    if (pipe2(stop_, O_CLOEXEC) < 0)
    {
        status_ = std::string("can't start reading: ") + strerror(errno);
        return;
    }
    thread_ = std::thread(&thhReader::run, this);
}

thhReader::~thhReader ()
{
    if (!thread_.joinable())
        return;

    const ssize_t n = write(stop_[1], "", 1);

    (void)n;    /* a pipe this new doesn't fill */
    thread_.join();
    close(stop_[0]);
    close(stop_[1]);
}

void
thhReader::notify ()
{
    if (!notified_.exchange(true))
        wake_();
}

void
thhReader::publish (const thhState &s)
{
    {
        std::lock_guard<std::mutex> l(lock_);

        if (s.buttons != published_.buttons ||
            s.connected != published_.connected)
            edges_.push_back(s);
        published_ = latest_ = s;
        fresh_ = true;
    }

    notify();
}

bool
thhReader::drain (std::vector<thhState> &edges, thhState &latest)
{
    /* Cleared first, so a publish that lands after the lock below wakes the
       drainer again rather than waiting on one that has already run. */
    notified_.store(false);

    std::lock_guard<std::mutex> l(lock_);

    if (!fresh_)
        return false;

    edges.clear();
    edges.swap(edges_);
    latest = latest_;
    fresh_ = false;
    return true;
}

std::string
thhReader::status () const
{
    std::lock_guard<std::mutex> l(lock_);

    return status_;
}

void
thhReader::run ()
{
    std::vector<int> fds;
    thhState s;
    int in = -1;

    for (;;)
    {
        if (in < 0)
        {
            std::string why;
            const bool ok = connect_(in, fds, why);
            bool changed;

            {
                std::lock_guard<std::mutex> l(lock_);

                changed = status_ != why;
                status_ = why;
            }
            if (changed)
                notify();

            if (ok)
            {
                s = thhState();
                s.connected = true;
                continue;
            }

            struct pollfd p = { stop_[0], POLLIN, 0 };

            if (poll(&p, 1, 1000) > 0)
                break;
            continue;
        }

        struct pollfd p[2] = { { in, POLLIN, 0 }, { stop_[0], POLLIN, 0 } };
        const int ready = poll(p, 2, silenceMs_);

        if (ready < 0 && errno == EINTR)
            continue;
        if (ready > 0 && p[1].revents)
            break;

        uint8_t buf[64];
        const ssize_t n = ready > 0 ? read(in, buf, sizeof(buf)) : -1;

        if (n < 0 && ready > 0 && (errno == EAGAIN || errno == EINTR))
            continue;

        /* Unplugged, asleep, or never woken: let go, and connect again. */
        if (n <= 0)
        {
            for (size_t i = 0; i < fds.size(); i++)
                close(fds[i]);
            fds.clear();
            in = -1;
            s = thhState();
            publish(s);
            continue;
        }

        if (thhIndexFeed(buf, n, s))
            publish(s);
    }

    for (size_t i = 0; i < fds.size(); i++)
        close(fds[i]);
}
