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
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include <zlib.h>

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

std::vector<thhIndexNodes>
thhFindIndexes (const std::string &sysRoot, const std::string &devRoot)
{
    /* Each interface's HID device sits in an interface directory under its
       USB device's, which is what ties a controller's nodes together. Keyed
       by that path, they come in the same order every time. */
    std::map<fs::path, thhIndexNodes> byUsb;
    std::vector<thhIndexNodes> out;
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
        if (thhIndexIsImu((const uint8_t *)desc.data(), desc.size()))
            n.imu = node;
    }

    for (auto &u : byUsb)
        if (!u.second.input.empty())
        {
            u.second.usb = u.first.string();
            u.second.serial = slurp(u.first / "serial");
            while (!u.second.serial.empty() &&
                   std::isspace((unsigned char)u.second.serial.back()))
                u.second.serial.pop_back();
            std::sort(u.second.all.begin(), u.second.all.end());
            out.push_back(u.second);
        }

    return out;
}

bool
thhFindIndex (const std::string &sysRoot, const std::string &devRoot,
              thhIndexNodes &out)
{
    const std::vector<thhIndexNodes> all = thhFindIndexes(sysRoot, devRoot);

    if (all.empty())
        return false;
    out = all[0];
    return true;
}

std::string
thhIndexHand (const std::string &compressed)
{
    std::string json(1 << 16, '\0');
    z_stream z = z_stream();

    if (inflateInit(&z) != Z_OK)
        return "";
    z.next_in = (Bytef *)compressed.data();
    z.avail_in = compressed.size();
    z.next_out = (Bytef *)&json[0];
    z.avail_out = json.size();

    const int ret = inflate(&z, Z_FINISH);

    json.resize(z.total_out);
    inflateEnd(&z);
    if (ret != Z_STREAM_END)
        return "";

    /* "model_number" : "Knuckles Left", or a revision of one, as JSON
       spells it; a scan rather than a parser, which is all one string
       needs. */
    const size_t key = json.find("\"model_number\"");
    const size_t open = key == std::string::npos ? key
                        : json.find('"', json.find(':', key + 14));
    const size_t close = open == std::string::npos ? open
                         : json.find('"', open + 1);

    if (close == std::string::npos)
        return "";

    const std::string model = json.substr(open + 1, close - open - 1);
    const auto ends = [&model](const char *with) {
        const std::string w = with;

        return model.size() >= w.size() &&
               model.compare(model.size() - w.size(), w.size(), w) == 0;
    };

    if (model.compare(0, 8, "Knuckles") != 0)
        return "";
    return ends(" Left") ? "left" : ends(" Right") ? "right" : "";
}

/* One feature report, as the controller gives them: it turns each request
   away once, then answers the same request a few milliseconds later, as
   SteamVR's lighthouse_console's own requests show; and it answers only a
   request of the report's whole size, its id and 64 bytes. */
static bool
getFeature (int fd, uint8_t id, uint8_t (&buf)[65])
{
    for (int attempt = 0; attempt < 10; attempt++)
    {
        memset(buf, 0, sizeof(buf));
        buf[0] = id;
        if (ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf) >= 0)
            return true;
        if (errno != EPIPE)
            return false;
        usleep(3000);
    }
    return false;
}

/* The configuration a controller keeps, read a feature report at a time
   (0x10 starts it and says how long it is; each 0x11 is a length and that
   many bytes, until a length of 0 or all of it) from whichever of its
   interfaces answers, the IMU's first, and the hand it names. Empty if
   none does. No more reads are made than the length it gave needs. */
static std::string
readHand (const std::vector<int> &fds)
{
    for (size_t i = 0; i < fds.size(); i++)
    {
        uint8_t buf[65];
        std::string config;

        if (!getFeature(fds[i], 0x10, buf))
            continue;

        const size_t expected = (buf[1] << 8) | buf[2];

        for (size_t k = 0; k < expected / (sizeof(buf) - 3) + 4 &&
                           config.size() < expected; k++)
        {
            if (!getFeature(fds[i], 0x11, buf) || buf[1] == 0 ||
                buf[1] > sizeof(buf) - 2)
                break;
            config.append((const char *)buf + 2, buf[1]);
        }

        const std::string hand = thhIndexHand(config);

        if (!hand.empty())
            return hand;
    }
    return "";
}

bool
thhConnectIndex (const std::string &sysRoot, const std::string &devRoot,
                 const std::string &usb, int &input, int &imu,
                 std::vector<int> &fds, std::string &hand,
                 std::string &status)
{
    const std::vector<thhIndexNodes> all = thhFindIndexes(sysRoot, devRoot);
    thhIndexNodes n;
    bool found = false;

    for (size_t i = 0; i < all.size() && !found; i++)
        if (usb.empty() || all[i].usb == usb)
        {
            n = all[i];
            found = true;
        }

    if (!found)
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

    imu = -1;
    for (size_t i = 0; i < n.all.size(); i++)
    {
        if (n.all[i] == n.input)
            input = opened[i];
        if (n.all[i] == n.imu)
            imu = opened[i];
    }
    /* The IMU's interface first: libsurvive reads the configuration there. */
    std::vector<int> ask;

    for (size_t i = 0; i < n.all.size(); i++)
        if (n.all[i] == n.imu)
            ask.push_back(opened[i]);
    for (size_t i = 0; i < n.all.size(); i++)
        if (n.all[i] != n.imu)
            ask.push_back(opened[i]);
    hand = readHand(ask);
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

std::string
thhReader::hand () const
{
    std::lock_guard<std::mutex> l(lock_);

    return hand_;
}

void
thhReader::run ()
{
    std::vector<int> fds;
    thhState s;
    thhIndexImu imu;
    int in = -1, motion = -1;
    bool heard = false;         /* a report since the connect */

    for (;;)
    {
        if (in < 0)
        {
            std::string why, hand;
            const bool ok = connect_(in, motion, fds, hand, why);
            bool changed;

            {
                std::lock_guard<std::mutex> l(lock_);

                /* A read that couldn't say keeps the hand it said before. */
                changed = status_ != why || (ok && !hand.empty() &&
                                             hand_ != hand);
                status_ = why;
                if (ok && !hand.empty())
                    hand_ = hand;
            }
            if (changed)
                notify();

            if (ok)
            {
                s = thhState();
                s.connected = true;
                imu = thhIndexImu();
                heard = false;
                continue;
            }

            struct pollfd p = { stop_[0], POLLIN, 0 };

            if (poll(&p, 1, 1000) > 0)
                break;
            continue;
        }

        /* poll() passes over a negative fd, so a controller with no IMU
           is only its input. */
        struct pollfd p[3] = { { in, POLLIN, 0 }, { motion, POLLIN, 0 },
                               { stop_[0], POLLIN, 0 } };
        const int ready = poll(p, 3, silenceMs_);

        if (ready < 0 && errno == EINTR)
            continue;
        if (ready > 0 && p[2].revents)
            break;

        uint8_t buf[64];
        const int from = ready > 0 && p[0].revents ? in
                         : ready > 0 && p[1].revents ? motion : -1;
        const ssize_t n = from >= 0 ? read(from, buf, sizeof(buf)) : -1;

        if (n < 0 && from >= 0 && (errno == EAGAIN || errno == EINTR))
            continue;

        /* Unplugged, asleep, or never woken: let go, and connect again. */
        if (n <= 0)
        {
            for (size_t i = 0; i < fds.size(); i++)
                close(fds[i]);
            fds.clear();
            in = motion = -1;
            s = thhState();
            publish(s);

            /* As after a connect that fails: a controller that goes as soon
               as it is woken is not woken again at once. */
            struct pollfd w = { stop_[0], POLLIN, 0 };

            if (poll(&w, 1, 1000) > 0)
                break;
            continue;
        }

        s.time = std::chrono::duration<double>(
                     std::chrono::steady_clock::now().time_since_epoch())
                     .count();
        /* Nothing is heard of a controller until its input has said where
           its buttons and axes are. */
        bool fed;

        /* A controller at rest sends no input report until something on
           it moves, and its IMU speaks at once: so any report, its ping
           included, is enough to know it is there, its buttons and axes at
           rest until its input says otherwise. */
        const bool ping = from == in && n >= 4 && buf[0] == 0x01 &&
                          buf[2] == 0x04 && buf[3] == 0x0b;

        fed = from == in ? thhIndexFeed(buf, n, s) || (ping && !heard)
                         : thhIndexFeedImu(buf, n, imu, s);
        heard = heard || fed;
        if (fed)
            publish(s);
    }

    for (size_t i = 0; i < fds.size(); i++)
        close(fds[i]);
}
