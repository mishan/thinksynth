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

/* thinkbox's voices of one compiled patch, apart from the BOX's tasks and
   clock, so the desktop can play them against the engine
   (scripts/compilecheck). */
#ifndef BOX_POOL_H
#define BOX_POOL_H 1

#include <stdint.h>
#include <string.h>

#include "thKernel.h"
#include "thhMapper.h"

#define VOICES 8
#define BLOCK 128
#define BOX_RATE 32000
#define BLOCK_US (1000000LL * BLOCK / BOX_RATE)
/* The top of a channel's amp, the engine's MIDIVALMAX. */
#define BOX_AMP_MAX 127

/* A patch, whichever compiled voice it is: its voices, its knobs, and
   what a mapper's actions do to them. */
struct Patch
{
    const char *name;
    float gain;                 /* to peak as loud as the others */
    /* What a voice of it costs of a block, as last seen: a voice starting
       takes its cost on one of the two cores, so half of it on the block.
       Its most of a block while playing, and the blocks it ran late. */
    float perVoice = 0, peak = 0;
    int late = 0;
    /* Each half's time rendering the last block, and its voices. */
    int64_t spent[2] = { 0, 0 };
    int rendered[2] = { 0, 0 };
    /* The channel's amp, 0..BOX_AMP_MAX as the desktop's: as set, and
       glided toward as the engine glides it, this block's over BOX_AMP_MAX
       a sample each. */
    float amp = BOX_AMP_MAX;
    float ampNow = 0;
    bool ampPrimed = false, ampSteady = false;
    float ampBlock[BLOCK];

    virtual ~Patch () {}
    virtual thcHidBlock profile () const = 0;
    virtual thcHidBlock marbles () const = 0;
    virtual thcHidBlock life () const = 0;
    /* owner: whose the note is, a finger's, a ball's or Life's, so a
       NOTE_OFF or a bend reaches only the voice its own NOTE_ON started.
       room: a free voice may be taken, the blocks not near their time.
       True where a NOTE_ON started a voice that wasn't sounding. */
    virtual bool play (const thhAction &a, int owner, int64_t now, bool room,
                       float level) = 0;
    /* Every sounding voice of one half, even or odd, into l and r, added:
       the two halves on the two cores at once, after glide(). */
    virtual void render (float *l, float *r, int half) = 0;
    /* The voice shed would take, as a rank, lower first; INT64_MAX for
       none. */
    virtual int64_t oldest () const = 0;
    /* A voice faded out over the next block and freed, when the blocks
       run late: a released one before a held one, the oldest first. */
    virtual void shed () = 0;
    virtual int sounding () const = 0;
    /* A chanarg as the scheduler wrote it: the channel's amp, or a knob
       the patch declares. */
    virtual void set (const char *name, float value) = 0;
    /* Every voice faded out over the next block, as a shed one is. */
    virtual void silence () = 0;

    /* The block's amp, once for both halves; nothing to do on the block
       after one that began where the amp still is, which every screen
       but the composer's always is. */
    void glide ()
    {
        if (!ampPrimed || !thkFinite(amp) || !thkFinite(ampNow))
            ampNow = amp;
        ampPrimed = true;

        const bool steady = ampNow == amp;

        if (steady && ampSteady)
            return;
        ampSteady = steady;
        thGlide(ampBlock, BLOCK, ampNow, amp, TH_SMOOTH_MS, BOX_RATE);
        for (int i = 0; i < BLOCK; i++)
            ampBlock[i] /= BOX_AMP_MAX;
    }

    void account ()
    {
        const int n = rendered[0] + rendered[1];

        if (n > 0)
            perVoice += 0.1f * ((spent[0] + spent[1]) / (1e6f * BLOCK / BOX_RATE) / n -
                                perVoice);
    }
};

template <class V>
struct PatchOf : Patch
{
    V voice[VOICES];
    typename V::Knobs knobs;
    struct
    {
        int     note = -1, owner = -1;  /* as played, and whose */
        bool    held = false, sounding = false, fading = false;
        /* Taken over: the note to start as the last fades out. */
        bool    next = false;
        int     vel = 0;
        float   level = 1;
        int64_t since = 0;          /* when it started, to steal the oldest */
    } slot[VOICES];

    PatchOf (const char *n, float g) { name = n; gain = g; }

    thcHidBlock profile () const override { return V::profile_box(); }
    thcHidBlock marbles () const override { return V::profile_marbles(); }
    thcHidBlock life () const override { return V::profile_life(); }

    /* Released before held, the oldest first. */
    int64_t rank (int i) const
    {
        return slot[i].since + (slot[i].held ? (int64_t)1 << 62 : 0);
    }

    int pick () const
    {
        int v = -1;

        for (int i = 0; i < VOICES; i++)
            if (slot[i].sounding && !slot[i].fading && (v < 0 || rank(i) < rank(v)))
                v = i;
        return v;
    }

    void start (int v)
    {
        voice[v] = V();
        voice[v].start(slot[v].note, slot[v].vel, (uint32_t)slot[v].since);
        slot[v].sounding = true;
        slot[v].fading = slot[v].next = false;
    }

    bool play (const thhAction &a, int owner, int64_t now, bool room,
               float level) override
    {
        int v = -1;

        switch (a.kind)
        {
        case thhAction::NOTE_ON:
            /* A free voice, on the core with fewer sounding -- unless one
               more voice would take the blocks near their time. */
            if (room)
            {
                int busy[2] = { 0, 0 };

                for (int i = 0; i < VOICES; i++)
                    busy[i % 2] += slot[i].sounding;
                for (int i = 0; i < VOICES && v < 0; i++)
                    if (!slot[i].sounding && i % 2 == (busy[1] < busy[0]))
                        v = i;
                for (int i = 0; i < VOICES && v < 0; i++)
                    if (!slot[i].sounding)
                        v = i;
            }
            /* Else a sounding one, faded out as a shed one is and the
               note started after it: one fading already, or the one shed
               would take. */
            for (int i = 0; i < VOICES && v < 0; i++)
                if (slot[i].sounding && slot[i].fading && !slot[i].next)
                    v = i;
            if (v < 0)
                v = pick();
            if (v < 0)
                v = 0;              /* each taken over already */
            slot[v].note = a.note;
            slot[v].owner = owner;
            slot[v].vel = a.vel;
            slot[v].level = level;
            slot[v].held = true;
            slot[v].since = now;
            if (slot[v].sounding)
                slot[v].fading = slot[v].next = true;
            else
            {
                start(v);
                return true;
            }
            break;
        case thhAction::NOTE_OFF:
            for (int i = 0; i < VOICES; i++)
                if (slot[i].held && slot[i].note == a.note && slot[i].owner == owner)
                {
                    /* Not yet started, it never is. */
                    if (slot[i].next)
                        slot[i].next = false;
                    else
                        voice[i].release();
                    slot[i].held = false;
                }
            break;
        case thhAction::NOTE_BEND:
            for (int i = 0; i < VOICES; i++)
                if (slot[i].held && slot[i].note == a.note && slot[i].owner == owner)
                    voice[i].note = a.note + (float)a.t;
            break;
        case thhAction::VALUE:
        {
            float lo = 0, hi = 1;

            if (V::Knobs::range(a.arg.c_str(), lo, hi))
                knobs.set(a.arg.c_str(), (float)thhActionValue(a, lo, hi));
            break;
        }
        default:
            break;
        }
        return false;
    }

    void render (float *l, float *r, int half) override
    {
        float vl[BLOCK], vr[BLOCK];
        float *outs[2] = { vl, vr };

        rendered[half] = 0;
        for (int v = half; v < VOICES; v += 2)
            if (slot[v].sounding)
            {
                const float level = slot[v].level;

                memset(vr, 0, sizeof(vr));
                slot[v].sounding = voice[v].render(knobs, outs, BLOCK);
                rendered[half]++;
                for (int i = 0; i < BLOCK; i++)
                {
                    /* A shed voice down to nothing over the block. */
                    const float g = gain * (slot[v].fading ? 1 - (i + 1) / (float)BLOCK : 1);

                    /* As the engine's channel mixes it, amp then level. */
                    l[i] += vl[i] * ampBlock[i] * level * g;
                    /* A mono patch is both sides. */
                    r[i] += (V::channels > 1 ? vr[i] : vl[i]) * ampBlock[i] * level * g;
                }
                if (slot[v].fading && slot[v].next)
                    start(v);
                else if (slot[v].fading)
                    slot[v].sounding = slot[v].held = slot[v].fading = false;
            }
    }

    int sounding () const override
    {
        int n = 0;

        for (int i = 0; i < VOICES; i++)
            n += slot[i].sounding;
        return n;
    }

    int64_t oldest () const override
    {
        const int v = pick();

        return v < 0 ? INT64_MAX : rank(v);
    }

    void shed () override
    {
        const int v = pick();

        if (v >= 0)
            slot[v].fading = true;
    }

    void set (const char *name, float value) override
    {
        if (!strcmp(name, "amp"))
            amp = value;
        else
            knobs.set(name, value);
    }

    void silence () override
    {
        for (int i = 0; i < VOICES; i++)
            if (slot[i].sounding)
            {
                slot[i].fading = true;
                slot[i].next = false;
            }
    }
};

#endif
