/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#include "config.h"

#include "gthMidiQueue.h"

gthMidiQueue::gthMidiQueue (void)
    : drainHook_(NULL), drainHookUser_(NULL), notified_(false), overflows_(0)
{
    /* A Glib::Dispatcher must be constructed on the thread that will receive
       from it. Constructing this object on the GUI thread is therefore part
       of its contract. */
    dispatcher_.connect(sigc::mem_fun(*this, &gthMidiQueue::drain));
}

bool gthMidiQueue::push (const gthMidiEvent &ev)
{
    if (!queue_.push(ev))
    {
        overflows_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    /* Only wake the main loop if it is not already awake and heading for the
       queue. A controller sweep is thousands of messages a second, and every
       emit() is a write to a pipe that can fill and block the producer. */
    if (!notified_.exchange(true, std::memory_order_acq_rel))
        dispatcher_.emit();

    return true;
}

void gthMidiQueue::drain (void)
{
    /* Cleared before draining, not after.
     *
     * If it were cleared afterwards, a message pushed during the drain would
     * find notified_ still true, skip its emit(), and then be left sitting in
     * the queue until some later message happened to wake the loop again --
     * a note that arrives at the wrong moment would hang until the next one.
     *
     * Clearing first means the worst case is a redundant wake-up on an empty
     * queue, which costs one pipe write and a loop iteration that finds
     * nothing.
     *
     * dspmidi's phase 3 holds this down: it pushes from another thread from
     * inside the hook below, which is the instant this store protects. Move
     * the store beneath the hook and that check fails every run. */
    notified_.store(false, std::memory_order_release);

    gthMidiEvent ev;

    while (queue_.pop(ev))
        sigEvent_(ev);

    /* The window, for anyone standing in it deliberately: after the last
       failed pop, where a push must still find notified_ clear and emit.
       NULL in every build but the harness's -- see setDrainHook. */
    if (drainHook_)
        drainHook_(drainHookUser_);
}
