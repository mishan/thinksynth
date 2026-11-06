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

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "thhControls.h"

/* Linux only: hidraw, poll() and a thread.

   The /dev nodes of one wired Index controller: the input interface's, and
   all of them, every one of which is woken. */
struct thhIndexNodes
{
    std::string              input;
    std::vector<std::string> all;
};

/* The first wired Index controller under sysRoot (normally /sys), matched by
   vendor, product and name, since a 2018 Vive Tracker has the same ID. Its
   nodes are named under devRoot (normally /dev). */
bool thhFindIndex (const std::string &sysRoot, const std::string &devRoot,
                   thhIndexNodes &out);

/* Reads a controller on a thread of its own and hands what it reads to
   another thread, which drains it.

   connect opens a controller, giving its input fd and every fd to close
   with it, or says why it can't (status); it is asked again a second later.
   wake is called on the reader's thread when there is something to drain,
   once until the next drain. */
class thhReader
{
public:
    typedef std::function<bool (int &input, std::vector<int> &fds,
                                std::string &status)> Connect;

    /* A controller silent for silenceMs is taken as gone: it pings about
       once a second while it is awake. */
    thhReader (const Connect &connect, const std::function<void ()> &wake,
               int silenceMs = 3000);
    ~thhReader ();

    /* What arrived since the last drain: each state at which a button or
       the connection changed, in order, so no press is lost to a slow
       drain, then the latest. False if nothing did. A connect is heard
       with the first report after it, so a controller's axes never read
       as zero before it has said where they are. */
    bool drain (std::vector<thhState> &edges, thhState &latest);

    /* Why there is no controller, or empty; a change wakes the drainer. */
    std::string status () const;

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
    std::string           status_;

    void run ();
    void publish (const thhState &s);
    void notify ();
};

/* The connect for a real controller: thhFindIndex under /sys and /dev,
   each node opened and sent the controller's wake-up. */
bool thhConnectIndex (const std::string &sysRoot, const std::string &devRoot,
                      int &input, std::vector<int> &fds, std::string &status);

#endif /* THH_READER_H */
