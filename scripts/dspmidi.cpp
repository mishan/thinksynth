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
 * A third phase covers the ordering inside gthMidiQueue::drain, which clears
 * notified_ *before* the pop loop rather than after. Clearing after leaves a
 * window -- between the pop loop's last failed pop and the store -- in which a
 * push sees notified_ still set, skips its emit(), and strands a message until
 * the next one arrives.
 *
 * That window is about two instructions wide, and racing for it does not work:
 * the flood phase was run against a deliberately inverted build and did not
 * catch it in 60,000 messages, because hitting two instructions from another
 * thread on a 20us cadence essentially does not happen. So phase 3 does not
 * race. gthMidiQueue::setDrainHook calls into the harness at precisely that
 * point, and the harness pushes from another thread while standing there.
 * Deterministic in both directions: it passes every run as the code stands,
 * and fails every run with the store moved down past the hook. Past the pop
 * loop but still above the hook is not the same edit and does not fail --
 * the window this stands in is the gap between the last failed pop and the
 * store, so the store has to end up on the far side of it.
 *
 * No MIDI device is needed or used.
 *
 * Copyright (C) 2004-2014 Metaphonic Labs. GPL 2 or later.
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

/* Phase 3: the drain-window ordering, deliberately rather than by racing.
 *
 * gthMidiQueue::drain clears notified_ before the pop loop. Cleared after, a
 * push landing between the last failed pop and the store sees the flag still
 * set, skips its emit(), and the message sits in the queue until some later
 * one happens to wake the loop.
 *
 * The queue's drain hook is called at exactly that point, so instead of hoping
 * another thread lands in a two-instruction window, this stands in it: push
 * one message from a second thread from inside the hook, then require it to
 * arrive. Correct, notified_ is already false, so the push emits and the
 * message lands on the next iteration. Inverted, the push is silent and
 * nothing ever wakes the loop again.
 *
 * A second thread and not this one, because push() from inside the hook would
 * be the same thread that is mid-drain -- which is not the situation, and
 * would be delivered by the very pop loop that is finishing. */
struct WindowCheck {
    gthMidiQueue queue;
    Glib::RefPtr<Glib::MainLoop> loop;

    unsigned long seen;
    bool fired;

    WindowCheck (void) : seen(0), fired(false) { }

    void onEvent (const gthMidiEvent &) {
        if (++seen >= 2)
            loop->quit();
    }

    /* Called at the end of drain(), once. */
    static void hook (void *user)
    {
        WindowCheck *self = (WindowCheck *)user;

        if (self->fired)
            return;

        self->fired = true;

        gthMidiEvent ev;
        ev.status = 0x90; ev.data1 = 60; ev.data2 = 64; ev.len = 3;

        /* Joined before the hook returns, so the push has definitely happened
           inside the window rather than after drain() has moved on. */
        std::thread t([self, ev]() { self->queue.push(ev); });
        t.join();
    }
};

static int checkDrainWindow (void)
{
    WindowCheck w;

    w.loop = Glib::MainLoop::create();
    w.queue.signal_event().connect(sigc::mem_fun(w, &WindowCheck::onEvent));
    w.queue.setDrainHook(&WindowCheck::hook, &w);

    gthMidiEvent first;
    first.status = 0x90; first.data1 = 60; first.data2 = 100; first.len = 3;

    w.queue.push(first);

    bool timedOut = false;
    sigc::connection watchdog = Glib::signal_timeout().connect(
        [&w, &timedOut]() -> bool {
            timedOut = true;
            w.loop->quit();
            return false;
        }, 2000);

    w.loop->run();
    watchdog.disconnect();

    if (timedOut || w.seen < 2)
    {
        printf("FAIL  a message pushed inside the drain window was never "
               "delivered (%lu of 2) -- notified_ is being cleared after the "
               "pop loop, not before\n", w.seen);
        return 1;
    }

    printf("ok    a push inside the drain window still wakes the loop\n");

    return 0;
}

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

    bad += checkDrainWindow();

    return bad;
}
