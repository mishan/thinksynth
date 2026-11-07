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
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

#include "thhReader.h"

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
    thhConnection c;
    thhState s;
    bool up = false;

    for (;;)
    {
        if (!up)
        {
            std::string why;

            c = thhConnection();
            up = connect_(c, why);

            bool changed;

            {
                std::lock_guard<std::mutex> l(lock_);

                /* A read that couldn't say keeps the hand it said before. */
                changed = status_ != why || (up && !c.hand.empty() &&
                                             hand_ != c.hand);
                status_ = why;
                if (up && !c.hand.empty())
                    hand_ = c.hand;
            }
            if (changed)
                notify();

            if (up)
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

        std::vector<struct pollfd> p;

        for (size_t i = 0; i < c.read.size(); i++)
            p.push_back({ c.read[i], POLLIN, 0 });
        p.push_back({ stop_[0], POLLIN, 0 });

        const int ready = poll(p.data(), p.size(), silenceMs_);

        if (ready < 0 && errno == EINTR)
            continue;
        if (ready > 0 && p.back().revents)
            break;

        size_t from = 0;

        while (ready > 0 && from < c.read.size() && !p[from].revents)
            from++;

        uint8_t buf[64];
        const bool got = ready > 0 && from < c.read.size();
        const ssize_t n = got ? read(c.read[from], buf, sizeof(buf)) : -1;

        if (n < 0 && got && (errno == EAGAIN || errno == EINTR))
            continue;

        /* Unplugged, asleep, or never woken: let go, and connect again. */
        if (n <= 0)
        {
            for (size_t i = 0; i < c.fds.size(); i++)
                close(c.fds[i]);
            c = thhConnection();
            up = false;
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
        if (c.feed(from, buf, n, s))
            publish(s);
    }

    for (size_t i = 0; i < c.fds.size(); i++)
        close(c.fds[i]);
}
