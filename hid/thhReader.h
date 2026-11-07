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

#ifndef THH_READER_H
#define THH_READER_H

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "thhControls.h"

/* Linux only: poll() and a thread.

   What makes a report read from a connection's read[from] a state: true
   if it is heard. */
typedef std::function<bool (size_t from, const uint8_t *data, size_t len,
                            thhState &s)> thhFeed;

/* An open controller: the fds to read, every fd to close with it (those
   included), its feed, and its hand if it says. */
struct thhConnection
{
    std::vector<int> read, fds;
    thhFeed          feed;
    std::string      hand;
};

/* Reads a controller on a thread of its own and hands what it reads to
   another thread, which drains it.

   connect opens a controller, or says why it can't (status); it is asked
   again a second later.
   wake is called on the reader's thread when there is something to drain,
   once until the next drain. */
class thhReader
{
public:
    typedef std::function<bool (thhConnection &c, std::string &status)>
        Connect;

    /* A controller silent for silenceMs is taken as gone: an Index pings
       about once a second while it is awake. */
    thhReader (const Connect &connect, const std::function<void ()> &wake,
               int silenceMs = 3000);
    ~thhReader ();

    /* What arrived since the last drain: each state at which a button or
       the connection changed, in order, so no press is lost to a slow
       drain, then the latest. False if nothing did. A connect is heard
       with the first report after it, of any kind: a controller at rest
       sends no input report until something on it moves, so until one
       does its buttons and axes are at rest. */
    bool drain (std::vector<thhState> &edges, thhState &latest);

    /* Why there is no controller, or empty; a change wakes the drainer. */
    std::string status () const;

    /* The hand the controller last connected said it is, or empty. */
    std::string hand () const;

private:
    Connect               connect_;
    std::function<void ()> wake_;
    int                   silenceMs_;
    std::thread           thread_;
    int                   stop_[2];
    std::atomic<bool>     notified_;

    mutable std::mutex    lock_;
    std::vector<thhState> edges_;
    thhState              latest_, published_;
    bool                  fresh_;
    std::string           status_, hand_;

    void run ();
    void publish (const thhState &s);
    void notify ();
};

#endif /* THH_READER_H */
