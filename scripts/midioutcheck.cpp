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
 */

/*
 * midioutcheck -- does gthMidiOut send what it is handed, where and when?
 *
 * Through ports of its own that record each message and the monotonic time
 * it was sent, so no device and no MIDI system are needed: which port a
 * pattern and an override resolve to, the program change on attach, each
 * message sent at its time plus the delay rather than when it arrived, a
 * retrigger's note-off before its note-on, controllers scaled and not
 * repeated, expression ahead of a note, and a flush that drops what is
 * queued and ends what is sounding.
 *
 * Then once through RtMidi itself, into a virtual input port this opens
 * under a name of its own, so the bytes cross the system's MIDI layer and
 * still reach nothing that could sound. Skipped where the platform has no
 * virtual ports (Windows) or no MIDI system at all.
 */

#include <stdio.h>
#include <stdint.h>

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <glib.h>
#include <RtMidi.h>

#include "gthMidiOut.h"

static int failures = 0;

static void check (bool ok, const char *what, const std::string &detail = "")
{
    printf("%-52s %s%s%s\n", what, ok ? "ok" : "FAILED",
           detail.empty() ? "" : "  ", detail.c_str());

    if (!ok)
        failures++;
}

struct Sent
{
    std::string port;
    gint64 at;
    std::vector<uint8_t> bytes;
};

static std::mutex sentLock;
static std::vector<Sent> sent;

static int opened = 0;

class RecordingPort : public gthMidiOut::Port
{
public:
    explicit RecordingPort (const std::string &name) : name_(name)
    {
        opened++;
    }

    bool dead (void) const override { return killed; }

    /* Set to have every port so far act unplugged. */
    static bool killed;

    void send (const uint8_t *msg, size_t len) override
    {
        std::lock_guard<std::mutex> l(sentLock);

        sent.push_back({ name_, g_get_monotonic_time(),
                         std::vector<uint8_t>(msg, msg + len) });
    }

private:
    std::string name_;
};

bool RecordingPort::killed = false;

static std::vector<Sent> take (void)
{
    std::lock_guard<std::mutex> l(sentLock);
    std::vector<Sent> out;

    out.swap(sent);
    return out;
}

static std::string hex (const std::vector<Sent> &v)
{
    std::string s;
    char buf[16];

    for (const Sent &m : v)
    {
        s += "[";

        for (size_t i = 0; i < m.bytes.size(); i++)
        {
            snprintf(buf, sizeof buf, i ? " %02x" : "%02x", m.bytes[i]);
            s += buf;
        }

        s += "]";
    }

    return s;
}

static bool bytesAre (const Sent &m, std::vector<uint8_t> want)
{
    return m.bytes == want;
}

static void settle (int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static thcInstrument instrument (const std::string &midi, int channel,
                                 int program = -1)
{
    thcInstrument inst;

    inst.name = "ext";
    inst.midi = midi;
    inst.midiChannel = channel;
    inst.midiProgram = program;
    return inst;
}

int main (void)
{
    const std::vector<std::string> names = {
        "Midi Through:Midi Through Port-0 14:0",
        "Surge XT:Surge XT MIDI In 128:0",
        "Digitakt:Digitakt MIDI 1 24:0",
    };

    gthMidiOut out(
        [&names] { return names; },
        [](const std::string &name, std::string &) -> gthMidiOut::Port *
        { return new RecordingPort(name); });

    int changed = 0;

    out.onChanged([&changed] { changed++; });

    /* ---- names ---- */

    check(gthMidiOut::stableName(names[1]) == "Surge XT:Surge XT MIDI In",
          "a stable name drops ALSA's client:port");
    check(gthMidiOut::stableName("IAC Driver Bus 1") == "IAC Driver Bus 1",
          "and leaves a name without them alone");
    check(out.resolve("Surge XT") == names[1], "a substring picks the port");
    check(out.resolve("digitakt") == names[2], "ignoring case if it must");
    check(out.resolve("Moog") == "", "and no port answers for no match");

    out.setRoute("Moog", "Digitakt:Digitakt MIDI 1");
    check(out.resolve("Moog") == names[2],
          "a route names the port by its stable name");
    check(changed == 1 && out.routes().size() == 1,
          "and the host hears that the routes changed");

    out.setRoute("Surge XT", gthMidiOut::PLAY_ON_SYNTH);

    std::string why;

    check(!out.attach(0, instrument("Surge XT", 0), why) &&
          why == "set to play on this synth",
          "a route can keep an instrument on its dsp", why);
    out.setRoute("Surge XT", "");
    check(out.route("Surge XT").empty() && changed == 3,
          "an empty route is the pattern's own match again");

    check(!out.attach(0, instrument("Moog Voyager", 0), why) &&
          why.find("no MIDI output port matches") != std::string::npos,
          "no port is a refusal with a reason", why);

    /* ---- attach, program, timing ---- */

    out.setDelay(30);
    take();

    thcInstrument lead = instrument("Surge", 2, 11);  /* program 12 */
    thcMidiCC cutoff;

    cutoff.name = "cutoff";
    cutoff.cc = 74;
    cutoff.min = 100;
    cutoff.max = 1100;
    lead.ccs.push_back(cutoff);

    why.clear();
    check(out.attach(5, lead, why), "attach opens the matched port", why);
    check(out.routeOf(5) == "Surge XT:Surge XT MIDI In, channel 3",
          "and routes the channel there", out.routeOf(5));

    settle(250);

    {
        const std::vector<Sent> s = take();

        check(s.size() == 1 && bytesAre(s[0], { 0xc2, 11 }) &&
              s[0].port == names[1],
              "the program change goes at once", hex(s));
    }

    /* A line of notes handed over late and unevenly -- as a 20 ms timer
       hands them -- comes out on its stamps plus the delay. */
    const gint64 t0 = g_get_monotonic_time() + 20000;

    for (int i = 0; i < 8; i++)
    {
        const gint64 at = t0 + i * 25000;

        out.noteOn(5, 60 + i, 100, 1, at);
        out.noteOff(5, 60 + i, at + 12000);
    }

    settle(20 + 30 + 8 * 25 + 600);

    {
        const std::vector<Sent> s = take();
        size_t ons = 0;
        gint64 early = 0, late = 0;
        int prev = 59;
        bool ordered = true;

        for (const Sent &m : s)
            if ((m.bytes[0] & 0xf0) == 0x90)
            {
                const gint64 want = t0 + (m.bytes[1] - 60) * 25000 + 30000;

                early = std::max(early, want - m.at);
                late = std::max(late, m.at - want);
                ordered = ordered && m.bytes[1] == prev + 1;
                prev = m.bytes[1];
                ons++;
            }

        /* What is checked is what is not the machine's: never early --
           which is also what sending on arrival would be, every note here
           being queued up to 205 ms ahead of its time -- and in order.
           How late a thread wakes is the machine's, and a shared CI
           runner has woken one 150 ms late, so that is reported, and
           bounded only against a message that never goes. */
        check(ons == 8, "every note-on is sent", std::to_string(ons));
        check(early < 1000 && ordered && late < 1000000,
              "each at its stamp plus the delay, in order",
              std::to_string(early / 1000.0) + " ms early, " +
              std::to_string(late / 1000.0) + " ms late at worst");
        check(!s.empty() && bytesAre(s[0], { 0xb2, 11, 100 }),
              "expression 100 goes ahead of the first note", hex({ s[0] }));
        check(s.size() == 17, "and once only, at level 1",
              std::to_string(s.size()) + " messages");
    }

    /* ---- retrigger, stale off, expression ---- */

    {
        const gint64 now = g_get_monotonic_time() - 30000;

        out.noteOn(5, 64, 90, 1, now);
        out.noteOn(5, 64, 80, 0.7f, now);        /* the key again         */
        out.noteOff(5, 64, now);                 /* ends it               */
        out.noteOff(5, 64, now);                 /* nothing left to end   */
        settle(250);

        const std::vector<Sent> s = take();

        check(s.size() == 5 &&
              bytesAre(s[0], { 0x92, 64, 90 }) &&
              bytesAre(s[1], { 0xb2, 11, 70 }) &&
              bytesAre(s[2], { 0x82, 64, 64 }) &&
              bytesAre(s[3], { 0x92, 64, 80 }) &&
              bytesAre(s[4], { 0x82, 64, 64 }),
              "a retrigger ends the key first; a stray off is not sent",
              hex(s));
    }

    /* ---- controllers ---- */

    {
        const gint64 now = g_get_monotonic_time() - 30000;

        out.control(5, "cutoff", 600, now);      /* 64                    */
        out.control(5, "cutoff", 601, now);      /* still 64: not sent    */
        out.control(5, "cutoff", 5000, now);     /* clamped to 127        */
        out.control(5, "resonance", 1, now);     /* not mapped            */
        settle(250);

        const std::vector<Sent> s = take();

        check(s.size() == 2 && bytesAre(s[0], { 0xb2, 74, 64 }) &&
              bytesAre(s[1], { 0xb2, 74, 127 }),
              "a cc is scaled, clamped and not repeated", hex(s));
    }

    /* ---- the pitch wheel ---- */

    {
        thcInstrument wheel = instrument("Surge", 5);
        thcMidiCC bend;

        bend.name = "wheel";
        bend.bend = true;
        bend.cc = -1;
        bend.min = -2;
        bend.max = 2;
        wheel.ccs.push_back(bend);

        out.attach(11, wheel, why);
        settle(250);
        take();

        const gint64 now = g_get_monotonic_time() - 30000;

        out.control(11, "wheel", 0, now);       /* centered: 8192        */
        out.control(11, "wheel", 2, now);       /* all the way up        */
        out.control(11, "wheel", 9, now);       /* clamped: the same     */
        settle(250);

        std::vector<Sent> s = take();

        check(s.size() == 2 && bytesAre(s[0], { 0xe5, 0, 64 }) &&
              bytesAre(s[1], { 0xe5, 127, 127 }),
              "a bend is the pitch wheel, 14 bits, and not repeated",
              hex(s));

        out.flush(11);
        settle(250);
        s = take();

        check(s.size() == 1 && bytesAre(s[0], { 0xe5, 0, 64 }),
              "a flush puts a bent wheel back to center", hex(s));

        out.detach(11);
        take();
    }

    /* ---- flush ---- */

    {
        const gint64 now = g_get_monotonic_time();

        out.noteOn(5, 67, 100, 1, now - 30000);  /* sounding              */
        out.noteOn(5, 69, 100, 1, now + 500000); /* queued for later      */
        settle(250);
        take();

        out.flush(5);
        settle(600);

        const std::vector<Sent> s = take();

        check(s.size() == 1 && bytesAre(s[0], { 0x82, 67, 64 }),
              "a flush ends what sounds and drops what is queued", hex(s));
    }

    /* ---- detach ---- */

    {
        out.noteOn(5, 72, 100, 1, g_get_monotonic_time() - 30000);
        settle(250);
        take();

        out.detach(5);
        out.noteOn(5, 74, 100, 1, g_get_monotonic_time() - 30000);
        settle(250);

        const std::vector<Sent> s = take();

        check(s.size() == 1 && bytesAre(s[0], { 0x82, 72, 64 }) &&
              out.routeOf(5).empty(),
              "detach ends the channel's notes and its route", hex(s));
    }

    /* ---- the delay changing under queued notes ---- */

    {
        out.setDelay(200);

        thcInstrument dev = instrument("Surge", 0);

        out.attach(6, dev, why);
        settle(250);
        take();

        const gint64 t0 = g_get_monotonic_time();

        out.noteOn(6, 50, 100, 1, t0);
        out.setDelay(0);                       /* turned down while it waits */
        out.noteOff(6, 50, t0 + 50000);
        settle(400);

        const std::vector<Sent> s = take();
        std::vector<Sent> notes;

        for (const Sent &m : s)
            if ((m.bytes[0] & 0xe0) == 0x80)
                notes.push_back(m);

        check(notes.size() == 2 && bytesAre(notes[0], { 0x90, 50, 100 }) &&
              bytesAre(notes[1], { 0x80, 50, 64 }),
              "turning the delay down keeps a note's off after its on",
              hex(s));

        out.detach(6);
        out.setDelay(30);
        take();
    }

    /* ---- two engine channels on one device channel ---- */

    {
        thcInstrument a = instrument("Surge", 7), b = instrument("Surge", 7);

        out.attach(8, a, why);
        out.attach(9, b, why);
        settle(250);
        take();

        const gint64 now = g_get_monotonic_time() - 30000;

        out.noteOn(9, 64, 100, 1, now);         /* b holds 64            */
        out.noteOn(8, 67, 100, 1, now);         /* a holds 67            */
        settle(250);
        take();

        out.detach(8);
        settle(250);

        std::vector<Sent> s = take();

        check(s.size() == 1 && bytesAre(s[0], { 0x87, 67, 64 }),
              "detaching one ends its notes, not another's on the channel",
              hex(s));

        /* a takes 64 over from b; b's off for its own 64 is stale. */
        out.attach(8, a, why);
        out.noteOn(8, 64, 90, 1, g_get_monotonic_time() - 30000);
        out.noteOff(9, 64, g_get_monotonic_time() - 30000);
        settle(250);
        s = take();

        bool stale = false;

        for (size_t i = 0; i < s.size(); i++)
            if (bytesAre(s[i], { 0x87, 64, 64 }) && i + 1 == s.size())
                stale = true;

        check(!stale && s.size() >= 2 &&
              bytesAre(s[s.size() - 1], { 0x97, 64, 90 }),
              "an off for a key another channel took over is not sent",
              hex(s));

        out.detach(8);
        out.detach(9);
        take();
    }

    /* ---- a port that died is opened again ---- */

    {
        const int before = opened;

        RecordingPort::killed = true;
        out.attach(10, instrument("Surge", 0), why);
        RecordingPort::killed = false;

        check(opened == before + 1, "a dead port is opened again on attach",
              std::to_string(opened - before) + " opened");

        out.detach(10);
        take();
    }

    /* ---- through RtMidi, to a port of our own ---- */

    {
        const std::string sink = "midioutcheck-" + std::to_string(getpid());
        static std::mutex heardLock;
        static std::vector<std::vector<uint8_t> > heard;
        RtMidiIn *in = NULL;

        try
        {
            in = new RtMidiIn(RtMidi::UNSPECIFIED, sink);

            /* WinMM has no virtual ports, and says so with a warning
               rather than a throw. */
            if (in->getCurrentApi() == RtMidi::WINDOWS_MM)
                throw RtMidiError("WinMM has no virtual MIDI ports",
                                  RtMidiError::WARNING);

            in->openVirtualPort(sink);
            in->setCallback([](double, std::vector<unsigned char> *msg,
                               void *)
                            {
                                std::lock_guard<std::mutex> l(heardLock);

                                heard.push_back(std::vector<uint8_t>(
                                    msg->begin(), msg->end()));
                            });
        }
        catch (RtMidiError &e)
        {
            delete in;
            in = NULL;
            printf("%-52s SKIP  %s\n", "through RtMidi to a virtual port",
                   e.getMessage().c_str());
        }

        if (in != NULL)
        {
            gthMidiOut real("midioutcheck-out");
            std::string rwhy;

            real.setDelay(0);

            const bool ok = real.attach(0, instrument(sink, 9, 3), rwhy);

            check(ok, "a real port answers to its name", rwhy);

            if (ok)
            {
                const gint64 now = g_get_monotonic_time();

                real.noteOn(0, 36, 110, 1, now);
                real.noteOff(0, 36, now + 10000);
                settle(500);

                std::lock_guard<std::mutex> l(heardLock);

                check(heard.size() == 4 &&
                      heard[0] == std::vector<uint8_t>({ 0xc9, 3 }) &&
                      heard[1] == std::vector<uint8_t>({ 0xb9, 11, 100 }) &&
                      heard[2] == std::vector<uint8_t>({ 0x99, 36, 110 }) &&
                      heard[3] == std::vector<uint8_t>({ 0x89, 36, 64 }),
                      "the bytes arrive through RtMidi, in order",
                      std::to_string(heard.size()) + " messages");
            }

            real.detach(0);
            delete in;
        }
    }

    printf("\n%s\n", failures ? "midioutcheck FAILED" : "midioutcheck ok");

    return failures ? 1 : 0;
}
