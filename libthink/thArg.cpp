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

#include <string.h>
#include <stdio.h>

#include "think.h"

/* Ownership of value will transfer to us. Same goes for SetAllocatedArg. */
thArg::thArg(const string &name, float value)
{
    name_ = name;
    values_ = new float[1];
    values_[0] = value;
    len_ = 1;

    type_ = ARG_VALUE;
    nodePtrId_ = -1;
    argPtrId_ = -1;
    argPtr_ = NULL;
    index_ = -1;       /* so we know it has not been indexed yet */

    widgetType_ = HIDE;
    min_ = 0;
    max_ = MIDIVALMAX;
}

thArg::thArg(const string &name, const float *value, int len)
{
    name_ = name;
    len_ = (len > 0) ? len : 0;
    values_ = new float[(len_ > 0) ? len_ : 1];

    if (len_ > 0 && value != NULL) {
        memcpy(values_, value, len_*sizeof(float));
    }
    else {
        /* A zero-length ARG_VALUE would make operator[] and getBuffer() read
           values_[0], so keep one well-defined element around. */
        values_[0] = 0;
    }

    type_ = ARG_VALUE;
    nodePtrId_ = -1;   /* so we know it has not been set yet */
    argPtrId_ = -1;   /* so we know it has not been set yet */
    argPtr_ = NULL;
    index_ = -1;       /* so we know it has not been indexed yet */

    widgetType_ = HIDE;
    min_ = 0;
    max_ = MIDIVALMAX;
}

thArg::thArg(const string &name, const string &node, const string &value)
{
    name_ = name;
    values_ = NULL;
    len_ = 0;

    nodePtrName_ = node;
    argPtrName_ = value;

    type_ = ARG_POINTER;
    nodePtrId_ = -1;   /* so we know it has not been set yet */
    argPtrId_ = -1;   /* so we know it has not been set yet */
    argPtr_ = NULL;
    index_ = -1;       /* so we know it has not been indexed yet */

    widgetType_ = HIDE;
    min_ = 0;
    max_ = MIDIVALMAX;
}

thArg::thArg(const string &name, const string &chanarg)
{
    name_ = name;
    values_ = NULL;
    len_ = 0;

    argPtrName_ = chanarg;

    type_ = ARG_CHANNEL;
    nodePtrId_ = -1;   /* so we know it has not been set yet */
    argPtrId_ = -1;   /* so we know it has not been set yet */
    argPtr_ = NULL;
    index_ = -1;       /* so we know it has not been indexed yet */

    widgetType_ = HIDE;
    min_ = 0;
    max_ = MIDIVALMAX;
}


thArg::thArg (const thArg *copyArg)
{
    name_ = copyArg->name_;

    len_ = copyArg->len_;
    values_ = new float[(len_ > 0) ? len_ : 1];

    if (len_ > 0 && copyArg->values_ != NULL) {
        memcpy(values_, copyArg->values_, len_ * sizeof(float));
    }
    else {
        values_[0] = 0;
        len_ = (copyArg->values_ != NULL) ? len_ : 0;
    }

    comment_ = copyArg->comment_;
    label_ = copyArg->label_;
    units_ = copyArg->units_;
    min_ = copyArg->min_;
    max_ = copyArg->max_;
    widgetType_ = copyArg->widgetType_;

    nodePtrName_ = copyArg->nodePtrName_;
    argPtrName_ = copyArg->argPtrName_;
    type_ = copyArg->type_;
    nodePtrId_ = copyArg->nodePtrId_;
    argPtrId_ = copyArg->argPtrId_;
    argPtr_ = copyArg->argPtr_;
    index_ = copyArg->index_;
}

/* the equivalent of creating a thArg(NULL, NULL, 0) */
thArg::thArg (void)
{
    values_ = NULL;
    len_ = 0;
    type_ = ARG_VALUE;
    widgetType_ = HIDE;
    min_ = 0;
    max_ = MIDIVALMAX;
    nodePtrId_ = -1;   /* so we know it has not been set yet */
    argPtrId_ = -1;   /* so we know it has not been set yet */
    argPtr_ = NULL;
    index_ = -1;       /* so we know it has not been indexed yet */
}

thArg::~thArg(void)
{
    if (values_) {
        delete[] values_;
    }
}

/* Returns a buffer of `elements' floats, zeroed if it had to be (re)allocated.
 *
 * This used to hand back raw `new float[]' memory. Plugins keep their state in
 * args -- delay lines, filter history, oscillator phase -- and read it back
 * before writing it (delay/echo, delay/fir, filt/comb, filt/allpass and others
 * all do). A note's args are copy-constructed from the channel's prototype
 * tree, where they are single-valued, so the *first* window of every note
 * resizes each of them from 1 to windowlen and the plugin then read whatever
 * happened to be in that heap block.
 *
 * That is the burst of static on a note's attack that clears once the note
 * sustains: after the first window len_ already matches and the buffer is
 * simply reused, so from then on it holds real audio.
 *
 * It also explains why this was not obvious on the machines this was written
 * on. Freshly mapped pages come from the kernel zero-filled, so on a quiet heap
 * the garbage was usually silence. On a busy one -- more notes, more
 * allocation churn -- the block comes back holding the previous note's samples
 * at full scale instead.
 *
 * Note that a resize deliberately does not preserve the old contents: going
 * from a single value to a window means the old value was a placeholder, not
 * history worth keeping. Zero is the right initial state for a delay line.
 */
float *thArg::allocate (unsigned int elements)
{
    if (values_ == NULL) {
        values_ = new float[elements]();   /* () -> value-initialised */
        len_ = elements;
    }
    else if (len_ != elements) {
        delete[] values_;
        values_ = new float[elements]();
        len_ = elements;
    }

    return values_;
}

void thArg::setArg (const string &name, float value)
{
    values_ = allocate(1);
    values_[0] = value;
    name_ = name;
    len_ = 1;
    type_ = ARG_VALUE;
}

void thArg::setArg(const string &name, const float *value, int len)
{
    values_ = allocate(len);

    name_ = name;
    
    memcpy(values_, value, len * sizeof(float));

    len_ = len;
    type_ = ARG_VALUE;
}

void thArg::setArg(const string &name, const string &node, const string &value)
{
    name_ = name;
    nodePtrName_ = node;
    argPtrName_ = value;
    
    type_ = ARG_POINTER;
}

void thArg::setArg(const string &name, const string &chanarg)
{
    name_ = name;
    argPtrName_ = chanarg;

    type_ = ARG_CHANNEL;
}

void thArg::getBuffer (float *buffer, unsigned int size)
{
    unsigned int i, j;    

    if (type_ != ARG_VALUE) {
        return;
    }

    if (values_ == NULL || len_ == 0) {
        /* Nothing to repeat -- hand back silence rather than reading off the
           end of a zero-length allocation. */
        memset(buffer, 0, size * sizeof(float));
        return;
    }

    if (len_ == 1)
    {
        /* The single-value case is a knob: the GUI thread writes it from a
           slider callback while the audio thread reads it here, and routing
           every drag through the command queue would be silly. Load it once,
           atomically, so the read cannot tear -- last writer wins, which is
           exactly what a control surface wants. */
        float held;

        /* The generic __atomic_load, not __atomic_load_n: the _n form only
           takes integer and pointer types. */
        __atomic_load(&values_[0], &held, __ATOMIC_RELAXED);

        for (i = 0; i < size; i++)
            buffer[i] = held;

        return;
    }

    j = 0; /* depth into the arg float array (for the loop) */
    for (i = 0; i < size; i++)
    {
        buffer[i] = values_[j];
        if (++j >= len_)
        {
            j = 0;
        }
    }
}

/* Safe to call from the GUI thread on an arg the audio thread is reading, as
   long as the arg already holds exactly one value: allocate() is then a no-op,
   so this is a lone atomic store with no reallocation. That is the slider case.
   Growing or shrinking an arg is a different matter -- see setValue(float*,int)
   below and thSynth::setChanArg. */
void thArg::setValue(float value)
{
    if (values_ != NULL && len_ == 1)
    {
        /* Store straight into the existing buffer. Note that values_ itself is
           deliberately not reassigned: allocate(1) would return the same
           pointer, but the assignment is still an 8-byte non-atomic write to a
           member the audio thread reads in getBuffer(), which is a race even
           though the value never changes. */
        __atomic_store(&values_[0], &value, __ATOMIC_RELAXED);
    }
    else
    {
        values_ = allocate(1);
        values_[0] = value;
    }

    m_signal_arg_changed(this);
}

void thArg::setValue(float *values, int len)
{
    allocate(len);
    memcpy(values_, values, len*sizeof(float));
    m_signal_arg_changed(this);
}
