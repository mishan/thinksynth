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

#ifndef TH_SAMPLE_RING_H
#define TH_SAMPLE_RING_H 1

#include <atomic>
#include <cstring>

#include "thExport.h"

/* Lock-free single-producer / single-consumer ring of floats, moved in bulk.
 *
 * thRing is a slot per item, which is the right shape for a command and the
 * wrong one for audio: handing over a window of 1024 samples through it would
 * mean 1024 release stores where one will do. This is the same structure and
 * the same release/acquire argument -- head_ written only by the producer,
 * tail_ only by the consumer, one slot always left empty so full and empty are
 * distinguishable -- with memcpy in and out and a single publish per call.
 *
 * The producer is the audio thread and the consumer is the GUI thread.
 *
 * Overrun policy: a window that does not fit is dropped whole, and counted.
 * The audio thread cannot block and it must not advance tail_ (that is the
 * consumer's, and writing it from here would break the one invariant this
 * rests on), so the only choices are to drop new data or to corrupt data the
 * consumer is reading. Dropping is invisible on a scope; the count is what
 * lets the GUI say so rather than quietly show a signal with holes in it.
 *
 * Partial writes are deliberately not offered. Half a window in the ring is a
 * discontinuity that a spectrum would happily transform and draw as though it
 * were signal, which is worse than a gap the display knows about.
 */
class THINK_API thSampleRing {
public:
    /* capacity is in samples. One is reserved, so a ring can hold capacity-1.
       Allocating is the caller's job to do off the audio thread. */
    explicit thSampleRing (unsigned int capacity)
        : buf_(NULL), capacity_(capacity < 2 ? 2 : capacity),
          head_(0), tail_(0), dropped_(0)
    {
        buf_ = new float[capacity_]();
    }

    ~thSampleRing (void)
    {
        delete [] buf_;
    }

    unsigned int capacity (void) const { return capacity_; }

    /* ---- producer (audio thread) ---- */

    /* Copies n samples in. Returns false without writing anything if they do
       not all fit, having counted the drop. */
    bool write (const float *samples, unsigned int n)
    {
        if (samples == NULL || n == 0)
            return true;    /* nothing to say, and not a drop */

        if (n > capacity_ - 1 || n > writable())
        {
            dropped_.fetch_add(n, std::memory_order_relaxed);
            return false;
        }

        const unsigned int head = head_.load(std::memory_order_relaxed);
        const unsigned int first = (head + n > capacity_) ? capacity_ - head : n;

        memcpy(buf_ + head, samples, first * sizeof(float));

        if (first < n)
            memcpy(buf_, samples + first, (n - first) * sizeof(float));

        head_.store((head + n) % capacity_, std::memory_order_release);

        return true;
    }

    /* Samples that would fit right now. Producer side. */
    unsigned int writable (void) const
    {
        const unsigned int head = head_.load(std::memory_order_relaxed);
        const unsigned int tail = tail_.load(std::memory_order_acquire);

        /* -1 for the slot that is always left empty */
        return (tail + capacity_ - head - 1) % capacity_;
    }

    /* ---- consumer (GUI thread) ---- */

    /* Copies out at most n samples, returning how many it moved. */
    unsigned int read (float *out, unsigned int n)
    {
        if (out == NULL || n == 0)
            return 0;

        const unsigned int have = readable();

        if (n > have)
            n = have;

        if (n == 0)
            return 0;

        const unsigned int tail = tail_.load(std::memory_order_relaxed);
        const unsigned int first = (tail + n > capacity_) ? capacity_ - tail : n;

        memcpy(out, buf_ + tail, first * sizeof(float));

        if (first < n)
            memcpy(out + first, buf_, (n - first) * sizeof(float));

        tail_.store((tail + n) % capacity_, std::memory_order_release);

        return n;
    }

    /* Samples waiting. Consumer side. */
    unsigned int readable (void) const
    {
        const unsigned int head = head_.load(std::memory_order_acquire);
        const unsigned int tail = tail_.load(std::memory_order_relaxed);

        return (head + capacity_ - tail) % capacity_;
    }

    bool empty (void) const { return readable() == 0; }

    /* Total samples the producer has had to throw away. Monotonic; the
       consumer reads it to decide whether to say so. */
    unsigned long dropped (void) const
    {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    /* Neither copyable nor assignable: two objects sharing one buffer would
       have two producers, which is the one thing this cannot survive. */
    thSampleRing (const thSampleRing &);
    thSampleRing &operator= (const thSampleRing &);

    float *buf_;
    unsigned int capacity_;

    std::atomic<unsigned int> head_;   /* written by the producer */
    std::atomic<unsigned int> tail_;   /* written by the consumer */

    std::atomic<unsigned long> dropped_;
};

#endif /* TH_SAMPLE_RING_H */
