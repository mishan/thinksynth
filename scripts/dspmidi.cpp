/*
 * dspmidi -- does MIDI get from a foreign thread to the main loop intact?
 *
 * The MIDI rework's risk is not "which library the bytes came from", it is
 * the thread handoff. gthALSAMidi got this for free: it handed the ALSA
 * sequencer's poll descriptors to Glib::signal_io(), so events arrived
 * already on the GUI thread. RtMidi delivers on a thread of its own, and
 * Windows has no file descriptor to hand Glib in the first place, so the
 * handoff had to be built: an SPSC ring plus a Glib::Dispatcher.
 *
 * What this covers: messages lost crossing the ring, messages reordered,
 * overflow handling under a producer faster than any real MIDI source, and a
 * main loop that stops being woken at all. Two phases, because they stress
 * different things -- a flood that fills the queue, then a trickle where most
 * drains end on an empty queue and each wake-up has to stand on its own.
 *
 * What this does NOT cover, stated plainly rather than implied: the ordering
 * inside gthMidiQueue::drain, which clears notified_ *before* the pop loop
 * rather than after. Clearing after leaves a window -- between the pop loop's
 * last failed pop and the store -- in which a push sees notified_ still set,
 * skips its emit(), and strands a message until the next one arrives. That
 * window is about two instructions wide. This harness was run against a
 * deliberately inverted build and did not catch it, at 60,000 messages,
 * because hitting two instructions from another thread on a 20us cadence
 * essentially does not happen.
 *
 * So that ordering is argued in the comment in gthMidiQueue::drain, not
 * tested here. Saying so is more useful than a test that appears to cover it.
 *
 * No MIDI device is needed or used.
 *
 * Copyright (C) 2004-2026 Metaphonic Labs. GPL 2 or later.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <atomic>
#include <thread>
#include <chrono>

#include <glibmm/main.h>
#include <glibmm/init.h>

#include "gthMidiQueue.h"

namespace {

struct Checker {
    gthMidiQueue queue;
    Glib::RefPtr<Glib::MainLoop> loop;

    unsigned long expected;
    unsigned long seen;
    unsigned long outOfOrder;
    unsigned long lastSeq;
    bool started;

    Checker (unsigned long n)
        : expected(n), seen(0), outOfOrder(0), lastSeq(0), started(false) { }

    /* The sequence number is carried in the two data bytes, 14 bits, so it
       wraps every 16384 -- enough to catch reordering without needing a
       wider event. */
    void onEvent (const gthMidiEvent &ev)
    {
        const unsigned long seq = ((unsigned long)ev.data1 << 7) | ev.data2;

        if (started)
        {
            const unsigned long want = (lastSeq + 1) & 0x3fff;

            if (seq != want)
                outOfOrder++;
        }

        started = true;
        lastSeq = seq;
        seen++;

        if (seen >= expected)
            loop->quit();
    }
};

} /* namespace */

int main (int argc, char **argv)
{
    unsigned long count = 200000;

    if (argc > 1)
        count = strtoul(argv[1], NULL, 10);

    Glib::init();

    Checker checker(count);

    checker.loop = Glib::MainLoop::create();

    checker.queue.signal_event().connect(
        sigc::mem_fun(checker, &Checker::onEvent));

    std::atomic<unsigned long> pushed(0);

    /* One in twenty of the run is the trickle phase; that is plenty of
       chances to land in the window, and it keeps the test quick. */
    const unsigned long trickleAt = count - (count / 20 ? count / 20 : 1);

    /* Stand-in for RtMidi's callback thread. */
    std::thread producer([&]() {
        unsigned long seq = 0;

        while (pushed.load(std::memory_order_relaxed) < count)
        {
            gthMidiEvent ev;

            ev.status = 0x90;
            ev.data1 = (unsigned char)((seq >> 7) & 0x7f);
            ev.data2 = (unsigned char)(seq & 0x7f);
            ev.len = 3;

            if (checker.queue.push(ev))
            {
                seq = (seq + 1) & 0x3fff;

                const unsigned long n =
                    pushed.fetch_add(1, std::memory_order_relaxed) + 1;

                /* Trickle: slow enough that the consumer keeps up and most
                   drains end on an empty queue, which is where a missed
                   wake-up strands a message with nothing behind it to
                   rescue it. */
                if (n >= trickleAt)
                    std::this_thread::sleep_for(std::chrono::microseconds(20));
            }
            else
            {
                /* Queue full: let the consumer catch up rather than spinning
                   the overflow counter into the millions. A real MIDI source
                   cannot outrun this queue. */
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
    });

    /* A watchdog, because the failure this is really looking for is a stall:
       if the notify handshake is wrong the loop simply never wakes again, and
       a hung test is a bad test. */
    bool timedOut = false;

    sigc::connection watchdog = Glib::signal_timeout().connect_seconds(
        [&]() -> bool {
            if (checker.seen < checker.expected)
            {
                timedOut = true;
                checker.loop->quit();
            }
            return false;
        }, 30);

    checker.loop->run();

    watchdog.disconnect();

    producer.join();

    /* Anything pushed between the last wake-up and the loop quitting. */
    while (checker.seen < pushed.load(std::memory_order_acquire) && !timedOut)
    {
        const unsigned long before = checker.seen;

        Glib::MainContext::get_default()->iteration(false);

        if (checker.seen == before)
            break;
    }

    const unsigned long over = checker.queue.overflows();

    printf("pushed    %lu\n", pushed.load());
    printf("delivered %lu\n", checker.seen);
    printf("dropped   %lu (queue full)\n", over);
    printf("misordered %lu\n", checker.outOfOrder);

    int bad = 0;

    if (timedOut)
    {
        printf("FAIL  timed out with %lu of %lu delivered -- the main loop "
               "stopped being woken\n", checker.seen, checker.expected);
        bad++;
    }

    if (checker.outOfOrder)
    {
        printf("FAIL  %lu message(s) arrived out of order\n",
               checker.outOfOrder);
        bad++;
    }

    if (checker.seen != pushed.load())
    {
        printf("FAIL  %lu pushed but %lu delivered\n", pushed.load(),
               checker.seen);
        bad++;
    }

    /* push() only returns true when the message is queued, so a successful
       push must be delivered. Overflows are reported and rejected at the
       producer, never silently swallowed. */
    if (!bad)
        printf("ok    every queued message arrived once, in order\n");

    return bad;
}
