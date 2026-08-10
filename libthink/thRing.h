/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#ifndef TH_RING_H
#define TH_RING_H 1

#include <atomic>

/* Lock-free single-producer / single-consumer ring buffer.
 *
 * thinksynth has exactly two threads: the audio callback, and everything else.
 * MIDI events arrive through Glib::signal_io so they land on the GUI thread
 * alongside the keyboard, the sliders and the patch manager. That makes every
 * queue here strictly one producer and one consumer, which is the case this
 * can be done for without a lock.
 *
 * Correctness rests on head_ being written only by the producer and tail_ only
 * by the consumer, and on the release/acquire pairing: the producer's release
 * store to head_ publishes the slot it just wrote, and the consumer's acquire
 * load of head_ is what makes that write visible. Same in reverse for tail_,
 * which is what stops the producer from overwriting a slot still being read.
 *
 * One slot is always left empty so that a full ring is distinguishable from an
 * empty one, i.e. the usable capacity is CAPACITY-1.
 */
template <typename T, unsigned int CAPACITY>
class thRing {
public:
    thRing (void) : head_(0), tail_(0) { }

    /* Producer side only. Returns false if the ring is full; the caller owns
       whatever it was trying to hand over and has to deal with it. */
    bool push (const T &item)
    {
        const unsigned int head = head_.load(std::memory_order_relaxed);
        const unsigned int next = (head + 1) % CAPACITY;

        if (next == tail_.load(std::memory_order_acquire))
            return false;   /* full */

        slots_[head] = item;
        head_.store(next, std::memory_order_release);

        return true;
    }

    /* Consumer side only. Returns false if the ring is empty. */
    bool pop (T &item)
    {
        const unsigned int tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire))
            return false;   /* empty */

        item = slots_[tail];
        tail_.store((tail + 1) % CAPACITY, std::memory_order_release);

        return true;
    }

    bool empty (void) const
    {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    T slots_[CAPACITY];

    std::atomic<unsigned int> head_;   /* written by the producer */
    std::atomic<unsigned int> tail_;   /* written by the consumer */
};

#endif /* TH_RING_H */
