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

#ifndef THC_AUDITION_H
#define THC_AUDITION_H 1

/*
 * The host's ear: what stands behind thcAudition in libthink/thcomposer.h.
 *
 * A composer hands over a chanarg vector and a target; this renders the
 * chain's instrument with those values on a synth of its own, renders or
 * reads the target once and keeps its features, and answers with the
 * distance thSoundFeat.h measures. Rendering takes tens of milliseconds
 * a candidate and a tick may not, so the renders run on a worker thread
 * and a composer collects answers on a later tick. A harness that wants
 * a piece to replay exactly sets it synchronous instead, and every
 * answer is in by the time hear() returns.
 *
 * The private synth loads the same modules the audio thread is running,
 * through a plugin manager of its own. A module's module_init writes its
 * arg indices into file-scope globals, so a second init writes the same
 * numbers over themselves -- the reason thcScheduler shares one control
 * synth across chains applies here too, and is why this keeps one synth
 * for its lifetime rather than one per render.
 *
 * What an instrument is -- its .dsp, its chanargs in the engine's terms,
 * which one a chain sinks to -- is the scheduler's knowledge, and it is
 * resolved there, on the scheduler's thread, before a job is queued. The
 * worker sees file paths and numbers.
 */

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "thcomposer.h"
#include "thSoundFeat.h"

class thSynth;

class thcAuditioner
{
public:
    /* A patch and the values that make it an instrument. */
    struct Instrument
    {
        std::string dsp;
        std::vector<std::pair<std::string, float> > chanargs;
    };

    /* `pluginPath' is where the audio synth's modules came from. */
    explicit thcAuditioner (const std::string &pluginPath);
    ~thcAuditioner (void);

    /* Render inside hear(), on the caller's thread: slower ticks and an
       exact replay. For genwav and gencheck. */
    void setSynchronous (bool on) { synchronous_ = on; }

    /* `candidate' is the chain's instrument with the composer's values
       already folded in. `target' is either an instrument -- `targetInstrument'
       set, rendered once and remembered under `targetKey' -- or a sound
       file at `targetFile'. Returns a ticket, or -1 if nothing can be
       rendered. */
    int hear (const Instrument &candidate, const std::string &targetKey,
              const Instrument *targetInstrument, const std::string &targetFile);

    /* 1 with the distance, 0 while pending, -1 if the render failed. */
    int heard (int ticket, double *distance);

    /* Blocks until every queued job has an answer. */
    void drain (void);

    /* Answers that have been collected, for a harness to count. */
    int answered (void) const { return answered_; }

private:
    thcAuditioner (const thcAuditioner &);
    thcAuditioner &operator= (const thcAuditioner &);

    struct Job
    {
        int ticket;
        Instrument candidate;
        std::string targetKey;
        Instrument targetInstrument;
        bool targetIsFile;
        std::string targetFile;
    };

    struct Answer
    {
        bool ok;
        double distance;
    };

    void run (void);
    Answer judge (const Job &job);
    bool render (const Instrument &inst, int note, std::vector<float> &mono);
    const thsound::Features *targetOf (const Job &job, int &note);

    std::string pluginPath_;
    bool synchronous_;

    std::thread worker_;
    std::mutex lock_;
    std::condition_variable wake_;
    bool quit_;

    std::vector<Job> queue_;
    std::map<int, Answer> answers_;
    int nextTicket_;
    int answered_;

    /* Worker-thread state. */
    thSynth *synth_;
    thsound::Extractor extractor_;

    struct Target
    {
        thsound::Features features;
        int note;
        bool ok;
    };

    std::map<std::string, Target> targets_;
};

#endif /* THC_AUDITION_H */
