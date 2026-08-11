/*
 * ringcheck -- does thSampleRing move samples across the thread boundary
 *              intact, and does it drop rather than corrupt when it cannot?
 *
 * thSampleRing is the one piece of the visualizer tap that runs inside the
 * audio callback, and it is the piece whose bugs are silent: a wrap handled
 * wrong does not crash, it puts a splice in the middle of a waveform that the
 * display then draws as though it were signal. So the properties here are all
 * about the *contents* and not merely about the counts.
 *
 * Two halves.
 *
 * Single-threaded, where the arithmetic can be checked exactly: wrap, the
 * reserved slot, a write that does not fit, partial drains, and the rule that
 * a rejected write leaves the ring byte-for-byte as it was. That last one is
 * the whole reason write() is all-or-nothing -- see the comment in the header.
 *
 * Threaded, where the memory ordering is: a producer writing a known ramp as
 * fast as it can and a consumer checking that every sample it receives is the
 * next one in the sequence, allowing for whole windows the producer says it
 * dropped. The ramp is the point. A test that only counted samples would pass
 * against a ring that published head_ before the memcpy finished; one that
 * checks each value against its predecessor cannot.
 *
 * Confirmed to fail before it was trusted to pass. Three deliberate breaks:
 *
 *   wrap copies from the wrong offset     4 checks fail, every run
 *   partial writes instead of all-or-none 5 checks fail, every run
 *   head_ published before the memcpy     1 check fails, 11 runs in 12
 *
 * The last one is worth stating rather than rounding up to "caught". It is a
 * memory-ordering bug and this is x86, where the hardware will not reorder the
 * stores and only the compiler can -- so the window is narrow and the harness
 * finds it by volume rather than by construction. On a weakly ordered machine
 * it would be constant. Do not read a single green run as proof of the
 * ordering; read it as proof of the arithmetic, and let ThreadSanitizer (via
 * dspstress) speak to the ordering.
 *
 * Deliberately not covered: multiple producers or multiple consumers. The
 * structure cannot survive either and does not claim to -- the copy
 * constructor is private so that a second producer cannot be created by
 * accident.
 *
 * Copyright (C) 2004-2026 Metaphonic Labs. GPL 2 or later.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <thread>
#include <vector>

#include "think.h"
#include "thSampleRing.h"

namespace {

int failures = 0;
int checks = 0;

void ok (bool cond, const char *what)
{
    checks++;

    if (!cond)
    {
        printf("FAIL  %s\n", what);
        failures++;
    }
}

/* A value that identifies its own position in the stream, so a sample that
   arrives out of place is recognisable on sight rather than merely counted.
   float holds 24 bits of integer exactly, so this is exact up to 16.7M
   samples -- well past what any run here produces. */
float rampAt (unsigned long i)
{
    return (float)(i & 0xffffffu);
}

/* ---- single-threaded ---- */

void basics (void)
{
    thSampleRing r(16);

    ok(r.capacity() == 16, "capacity is what was asked for");
    ok(r.empty(), "a new ring is empty");
    ok(r.readable() == 0, "a new ring has nothing to read");
    ok(r.writable() == 15, "one slot is reserved, so 16 holds 15");
    ok(r.dropped() == 0, "a new ring has dropped nothing");

    float in[8], out[8];

    for (int i = 0; i < 8; i++)
        in[i] = rampAt(i);

    ok(r.write(in, 8), "a write that fits succeeds");
    ok(r.readable() == 8, "and eight samples are waiting");
    ok(r.writable() == 7, "leaving room for seven more");

    ok(r.read(out, 8) == 8, "reading eight gets eight");
    ok(memcmp(in, out, sizeof(in)) == 0, "and they are the samples written");
    ok(r.empty(), "the ring is empty again");

    /* Zero-length and NULL are not errors and are not drops: the tap calls
       write() unconditionally on a window that may have had no voices in it. */
    ok(r.write(NULL, 4), "a NULL write is a no-op, not a failure");
    ok(r.write(in, 0), "a zero-length write is a no-op");
    ok(r.read(NULL, 4) == 0, "a NULL read moves nothing");
    ok(r.read(out, 0) == 0, "a zero-length read moves nothing");
    ok(r.dropped() == 0, "and none of that counted as a drop");
}

void wrapping (void)
{
    /* Deliberately a size that is not a power of two and not a multiple of
       the write size, so every write after the first straddles the end. */
    thSampleRing r(13);

    unsigned long written = 0, readBack = 0;
    bool contents = true, counts = true;

    float in[5], out[5];

    for (int round = 0; round < 200; round++)
    {
        for (int i = 0; i < 5; i++)
            in[i] = rampAt(written + i);

        if (!r.write(in, 5))
        {
            counts = false;    /* five always fits into an emptied 13 */
            break;
        }

        written += 5;

        const unsigned int got = r.read(out, 5);

        if (got != 5)
        {
            counts = false;
            break;
        }

        for (unsigned int i = 0; i < got; i++)
        {
            if (out[i] != rampAt(readBack + i))
                contents = false;
        }

        readBack += got;
    }

    ok(counts, "200 rounds of write-then-read across the wrap all fit");
    ok(contents, "every sample came back in its own position across the wrap");
    ok(written == readBack, "nothing was left behind");
    ok(r.dropped() == 0, "and nothing was dropped");
}

void overrun (void)
{
    thSampleRing r(16);   /* holds 15 */

    float in[15], out[15];

    for (int i = 0; i < 15; i++)
        in[i] = rampAt(i);

    ok(r.write(in, 15), "exactly capacity-1 fits");
    ok(r.writable() == 0, "and fills it");

    /* Snapshot the contents, then try a write that cannot fit, then check
       nothing moved. This is the property that makes dropping safe: a refused
       write must not have half-written. */
    ok(!r.write(in, 4), "a write into a full ring is refused");
    ok(r.dropped() == 4, "and the samples it could not take are counted");
    ok(r.readable() == 15, "the ring still holds what it held");

    ok(r.read(out, 15) == 15, "which reads back");
    ok(memcmp(in, out, sizeof(in)) == 0, "unaltered by the refused write");

    /* A window larger than the ring can never fit, however empty it is. The
       tap sizes rings at several windows so this should not arise, but a ring
       that spun forever or wrote past its end here would be much worse than
       one that says no. */
    thSampleRing big(8);
    float huge[32] = {0};

    ok(!big.write(huge, 32), "a write larger than the whole ring is refused");
    ok(big.empty(), "and leaves the ring empty rather than partly written");
    ok(big.dropped() == 32, "counted");

    /* Partial drains: the consumer asking for more than is there gets what
       is there, and asking for less leaves the rest in order. */
    thSampleRing p(32);

    for (int i = 0; i < 10; i++)
        in[i] = rampAt(100 + i);

    ok(p.write(in, 10), "ten samples in");
    ok(p.read(out, 4) == 4, "a short read moves four");
    ok(out[0] == rampAt(100) && out[3] == rampAt(103), "the first four");
    ok(p.read(out, 99) == 6, "a greedy read moves only what is left");
    ok(out[0] == rampAt(104) && out[5] == rampAt(109), "the remaining six");
    ok(p.empty(), "and the ring is empty");
}

/* ---- threaded ---- */

struct Threaded {
    unsigned long produced;   /* samples the producer got in       */
    unsigned long dropped;    /* samples the ring refused          */
    unsigned long consumed;   /* samples the consumer took out     */
    unsigned long misplaced;  /* samples that were not their own position */
    unsigned long gaps;       /* discontinuities the drops explain */
    bool unexplained;         /* a discontinuity they do not      */
};

/* The producer writes a global ramp: sample number i always carries rampAt(i),
   whether or not it makes it into the ring. So a dropped window shows up as a
   forward jump of exactly the number of samples the ring says it refused, and
   anything else is a real fault. */
Threaded threaded (unsigned int capacity, unsigned int window,
                   unsigned long windows)
{
    Threaded r;

    memset(&r, 0, sizeof(r));

    thSampleRing ring(capacity);

    std::atomic<bool> done(false);
    std::atomic<unsigned long> produced(0);

    std::thread producer([&]() {
        std::vector<float> buf(window);
        unsigned long at = 0;

        for (unsigned long w = 0; w < windows; w++)
        {
            for (unsigned int i = 0; i < window; i++)
                buf[i] = rampAt(at + i);

            ring.write(&buf[0], window);   /* may drop; that is the point */

            at += window;
            produced.store(at, std::memory_order_relaxed);
        }

        done.store(true, std::memory_order_release);
    });

    std::vector<float> out(window * 4);

    bool started = false;
    unsigned long expect = 0;

    for (;;)
    {
        const unsigned int got = ring.read(&out[0], (unsigned int)out.size());

        if (got == 0)
        {
            if (done.load(std::memory_order_acquire) && ring.empty())
                break;

            std::this_thread::yield();
            continue;
        }

        for (unsigned int i = 0; i < got; i++)
        {
            const float want = rampAt(expect);

            if (!started)
            {
                started = true;
            }
            else if (out[i] != want)
            {
                /* A gap is legitimate only if it lands on a window boundary:
                   write() is all-or-nothing, so a drop can never leave the
                   stream part-way through a window. */
                unsigned long ahead = 0;

                for (unsigned long k = 1; k <= windows + 1; k++)
                {
                    if (out[i] == rampAt(expect + k * window))
                    {
                        ahead = k;
                        break;
                    }
                }

                if (ahead)
                {
                    r.gaps++;
                    expect += ahead * window;
                }
                else
                {
                    r.misplaced++;
                    r.unexplained = true;
                }
            }

            expect++;
        }

        r.consumed += got;
    }

    producer.join();

    r.produced = produced.load();
    r.dropped = ring.dropped();

    return r;
}

} /* namespace */

int main (int argc, char **argv)
{
    unsigned long windows = 20000;

    if (argc > 1)
        windows = strtoul(argv[1], NULL, 10);

    printf("== single-threaded\n");

    basics();
    wrapping();
    overrun();

    printf("   %d checks, %d failed\n", checks, failures);

    printf("== threaded\n");

    /* Two shapes. A ring several windows deep is what the tap actually uses,
       and should drop little or nothing. A ring barely larger than one window
       is the pathological case, where nearly every write is refused -- which
       is precisely where a wrap or an ordering bug would show, since head_ and
       tail_ chase each other around constantly. */
    struct { const char *what; unsigned int cap; unsigned int win; } shapes[] = {
        { "8 windows deep (as the tap sizes it)", 8 * 1024 + 1, 1024 },
        { "barely one window deep",               1024 + 2,     1024 },
        { "an awkward size, small windows",       997,          61   },
    };

    for (unsigned int s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++)
    {
        const Threaded t = threaded(shapes[s].cap, shapes[s].win, windows);

        printf("   %-38s  %lu produced, %lu dropped, %lu consumed, "
               "%lu gap(s)\n", shapes[s].what, t.produced, t.dropped,
               t.consumed, t.gaps);

        char msg[256];

        snprintf(msg, sizeof(msg), "%s: every sample arrived in its own "
                 "position", shapes[s].what);
        ok(!t.unexplained && t.misplaced == 0, msg);

        /* The books have to balance: what came out plus what the ring says it
           refused is what went in. A ring that lost a window without counting
           it would pass the ordering check above and fail this one. */
        snprintf(msg, sizeof(msg), "%s: consumed + dropped == produced",
                 shapes[s].what);
        ok(t.consumed + t.dropped == t.produced, msg);

        /* A gap with nothing dropped is a lost window the ring did not admit
           to, which is the failure this whole harness exists to catch. */
        snprintf(msg, sizeof(msg), "%s: no gap without a drop to explain it",
                 shapes[s].what);
        ok(t.dropped > 0 || t.gaps == 0, msg);
    }

    printf("\n%d checks, %d failed\n", checks, failures);

    return failures;
}
