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

#include "thcAudition.h"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <system_error>

#include "think.h"
#include "thSoundFile.h"

thcAuditioner::thcAuditioner (const std::string &pluginPath, double rate)
    : pluginPath_(pluginPath), rate_(rate), synchronous_(false), quit_(false),
      busy_(-1), busyForgotten_(false), nextTicket_(0), answered_(0),
      synth_(NULL), extractor_(rate)
{
}

thcAuditioner::~thcAuditioner (void)
{
    {
        std::lock_guard<std::mutex> hold(lock_);

        quit_ = true;
    }

    wake_.notify_all();

    if (worker_.joinable())
        worker_.join();

    delete synth_;
}

int
thcAuditioner::hear (const Instrument &candidate, const std::string &targetKey,
                     const Instrument *targetInstrument,
                     const std::string &targetFile)
{
    if (candidate.dsp.empty() || (targetInstrument == NULL && targetFile.empty()))
        return -1;

    Job job;

    job.ticket = nextTicket_++;
    job.candidate = candidate;
    job.targetKey = targetKey;
    job.targetIsFile = targetInstrument == NULL;

    if (targetInstrument)
        job.targetInstrument = *targetInstrument;
    else
        job.targetFile = targetFile;

    if (synchronous_)
    {
        answers_[job.ticket] = judge(job);
        return job.ticket;
    }

    {
        std::lock_guard<std::mutex> hold(lock_);

        queue_.push_back(job);

        /* Started on the first job rather than in the constructor, so a
           host that never listens never has a thread. */
        if (!worker_.joinable())
            worker_ = std::thread(&thcAuditioner::run, this);
    }

    wake_.notify_one();

    return job.ticket;
}

int
thcAuditioner::heard (int ticket, double *distance)
{
    std::lock_guard<std::mutex> hold(lock_);

    std::map<int, Answer>::iterator a = answers_.find(ticket);

    if (a == answers_.end())
        return 0;

    const Answer answer = a->second;

    answers_.erase(a);
    answered_++;

    if (!answer.ok)
        return -1;

    if (distance)
        *distance = answer.distance;

    return 1;
}

void
thcAuditioner::forget (int ticket)
{
    std::lock_guard<std::mutex> hold(lock_);

    if (ticket == busy_)
    {
        busyForgotten_ = true;
        return;
    }

    for (size_t i = 0; i < queue_.size(); i++)
        if (queue_[i].ticket == ticket)
        {
            queue_.erase(queue_.begin() + i);
            return;
        }

    answers_.erase(ticket);
}

void
thcAuditioner::drain (void)
{
    std::unique_lock<std::mutex> hold(lock_);

    wake_.wait(hold, [this] {
        return (queue_.empty() && busy_ < 0) || quit_;
    });
}

void
thcAuditioner::run (void)
{
    std::unique_lock<std::mutex> hold(lock_);

    for (;;)
    {
        wake_.wait(hold, [this] { return !queue_.empty() || quit_; });

        if (quit_)
            return;

        /* Off the queue before it renders, so forget() can take any
           job still on it without touching this one. */
        const Job job = queue_.front();

        queue_.erase(queue_.begin());
        busy_ = job.ticket;
        busyForgotten_ = false;

        hold.unlock();

        const Answer answer = judge(job);

        hold.lock();

        if (!busyForgotten_)
            answers_[job.ticket] = answer;

        busy_ = -1;

        /* drain() waits on the same variable. */
        wake_.notify_all();
    }
}

bool
thcAuditioner::render (const Instrument &inst, int note, std::vector<float> &mono)
{
    if (synth_ == NULL)
        synth_ = new thSynth(pluginPath_, TH_DEFAULT_WINDOW_LENGTH,
                             (int)rate_);

    return thsound::renderNote(*synth_, inst.dsp, inst.chanargs, note,
                               thsound::HOLD_WINDOWS, thsound::TAIL_WINDOWS,
                               mono, inst.effect);
}

/* A file's size and modification time, or nothing if it is not there. */
static std::string
stamp (const std::string &path)
{
    std::error_code e;
    const std::uintmax_t size = std::filesystem::file_size(path, e);

    if (e)
        return std::string();

    const std::filesystem::file_time_type when =
        std::filesystem::last_write_time(path, e);

    if (e)
        return std::string();

    std::ostringstream out;

    /* In nanoseconds, whose count is a long long everywhere: libc++'s
       file clock counts in an __int128 no ostream prints. */
    out << size << '@'
        << (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(
               when.time_since_epoch()).count();

    return out.str();
}

/* Everything a target sounds like: the file and when it was written, or
   the instrument's files, when they were written and every chanarg.
   A target whose print has changed since it was heard is heard again. */
static std::string
print (const thcAuditioner::Instrument &inst, bool isFile,
       const std::string &file)
{
    std::ostringstream out;

    if (isFile)
    {
        out << file << '|' << stamp(file);
        return out.str();
    }

    out << inst.dsp << '|' << stamp(inst.dsp) << '|'
        << inst.effect << '|' << stamp(inst.effect);

    out.precision(9);

    for (size_t i = 0; i < inst.chanargs.size(); i++)
        out << '|' << inst.chanargs[i].first << '=' << inst.chanargs[i].second;

    return out.str();
}

/* A target's features, rendered or read the first time it is asked for
   and kept until what it is changes: an edit, a knob its chanargs are
   bound to, another piece's instrument of the same name, a file written
   over. A sound file's note is what it sounds at; an instrument is heard
   at middle C. */
const thsound::Features *
thcAuditioner::targetOf (const Job &job, int &note)
{
    const std::string now = print(job.targetInstrument, job.targetIsFile,
                                  job.targetFile);
    std::map<std::string, Target>::iterator t = targets_.find(job.targetKey);

    if (t != targets_.end() && t->second.print != now)
    {
        targets_.erase(t);
        t = targets_.end();
    }

    if (t == targets_.end())
    {
        Target target;
        std::vector<float> mono;
        std::string why;

        target.ok = false;
        target.note = 60;
        target.print = now;

        if (job.targetIsFile)
        {
            if (thsound::readWav(job.targetFile, mono, why, rate_))
            {
                const int heard = thsound::detectNote(mono, rate_);

                if (heard >= 0)
                    target.note = heard;

                /* As long as a render, so the two have the same frames:
                   cut at the end of the release, or followed by the
                   silence that followed it. */
                mono.resize((size_t)(thsound::HOLD_WINDOWS + thsound::TAIL_WINDOWS) *
                            TH_DEFAULT_WINDOW_LENGTH, 0.0f);
                target.ok = true;
            }
        }
        else
            target.ok = render(job.targetInstrument, target.note, mono);

        if (target.ok)
        {
            extractor_.extract(mono, target.features);
            target.ok = target.features.usable();
        }

        t = targets_.insert(std::make_pair(job.targetKey, target)).first;
    }

    note = t->second.note;

    return t->second.ok ? &t->second.features : NULL;
}

thcAuditioner::Answer
thcAuditioner::judge (const Job &job)
{
    Answer answer = { false, 0.0 };
    int note = 60;

    const thsound::Features *target = targetOf(job, note);

    if (target == NULL)
        return answer;

    std::vector<float> mono;
    thsound::Features mine;

    if (!render(job.candidate, note, mono))
        return answer;

    extractor_.extract(mono, mine);

    if (!mine.usable())
        return answer;

    answer.ok = true;
    answer.distance = thsound::Extractor::distance(*target, mine).total();

    return answer;
}
