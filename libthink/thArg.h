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

#ifndef TH_ARG_H
#define TH_ARG_H 1

#include "thExport.h"

#include <sigc++/sigc++.h>

class thArg;

typedef sigc::signal<void(thArg *)> type_signal_arg_changed;

class THINK_API thArg {
public:
    thArg  (const string &name, float value);
    thArg  (const string &name, const float *value, int num);
    thArg  (const string &name, const string &node, const string &value);
    thArg  (const string &name, const string &chanarg);
    thArg  (const thArg *copyArg);
    thArg  (void);
    ~thArg (void);

    enum ArgType { ARG_VALUE = 0, ARG_POINTER, ARG_CHANNEL, ARG_NOTE };
    /* immidiate value, pointer to another node, pointer to a channel arg, or
       pointer to a note arg. */

    enum WidgetType { HIDE = 0, SLIDER, CHANARG };
    
    void setArg(const string &name, float value);
    void setArg(const string &name, const float *value, int len);
    void setArg(const string &name, const string &node, const string &value);
    void setArg(const string &name, const string &chanarg);

    /* Single-float writes are safe from the GUI thread while the audio thread
       reads: no reallocation, and the store/load pair is atomic. This is how
       sliders reach the graph. */
    void setValue(float value);

    /* NOT safe concurrently -- it can reallocate values_. Route through
       thSynth::setChanArg, which queues the swap for the audio thread. */
    void setValue(float *values, int len); /* set a longer arg */

    void setIndex (int i) { index_ = i; };
    int index (void) { return index_; };
    void getBuffer (float *buffer, unsigned int size);

    unsigned int len (void) { return len_; }

    const string &name (void) const { return name_; };
    const string &label (void) const { return label_; }

    /* An optional name for the group this control belongs to: "Envelope" for
       an ADSR's four sliders. Purely for presentation -- the engine never
       reads it -- but a patch's controls come in clumps and saying so lets an
       editor draw them as one thing rather than four unrelated rows. */
    const string &group (void) const { return group_; }
    const string &units (void) const { return units_; }
    const string &comment (void) const { return comment_; }

    /* 0 for a continuous parameter, 1 for one that means a whole number.
     *
     * Presentation, like label and group: nothing in the audio path rounds
     * anything, and a .dsp is still free to store 3.4 in an arg with a step of
     * 1. What it buys is a control that cannot *produce* 3.4 for a parameter
     * whose plugin reads it as `switch ((int)x)' and therefore cannot tell 3.4
     * from 3.
     *
     * Two ways in. A plugin declares it about its own arg through
     * thPlugin::setArgStep, and thSynthTree::typeChanArgs carries it from there
     * to whichever control drives it; or the .dsp says `@x.step = 1' itself. */
    float step (void) const { return step_; }

    /* The names of this arg's values, from 0 up, or empty for the ordinary case
       of a number that is just a number. An entry may be empty where a value
       exists in the range but means nothing -- osc::window implements waveforms
       0, 2 and 3 and not 1, and offering the gap would produce silence at best.

       A non-empty list implies a step of 1 and a range of 0..size()-1. */
    const vector<string> &valueNames (void) const { return valueNames_; }

    /* True if the .dsp itself said `@x.step' or `@x.values', in which case
       nothing may retype it on the author's behalf.

       Without this a file cannot say "leave this one continuous": a step of 0
       is the default, so an explicit refusal and having said nothing at all
       would be the same state. */
    bool typedByFile (void) const { return typedByFile_; }

    float min (void) const { return min_; }
    float max (void) const { return max_; }

    ArgType type (void) const { return type_; }
    WidgetType widgetType (void) const { return widgetType_; }

    void setLabel (const string &label) { label_ = label; };
    void setGroup (const string &group) { group_ = group; };
    void setUnits (const string &units) { units_ = units; };
    void setComment (const string &comment) { comment_ = comment; };
    void setMin (float min) { min_ = min; };
    void setMax (float max) { max_ = max; };
    void setWidgetType (WidgetType widgetType) { widgetType_ = widgetType; };

    /* `fromFile' distinguishes the parser saying so from the tree working it
       out. Only the parser passes true, and only the parser may. */
    void setStep (float step, bool fromFile = false) {
        step_ = step;
        if (fromFile) typedByFile_ = true;
    };

    void setValueNames (const vector<string> &names, bool fromFile = false) {
        valueNames_ = names;
        if (!names.empty()) step_ = 1;
        if (fromFile) typedByFile_ = true;
    };

    /* The `@x.values = "Sine,Sawtooth,Square"' spelling, split on commas with
       surrounding space trimmed. An empty entry is a value with no name, so
       "Sine,,Square" names 0 and 2 and leaves 1 unnamed -- which is what a
       plugin implementing some of a range and not the rest needs to say.

       Here rather than in the grammar action because the node editor's writer
       has to produce this string as well as read it, and one spelling wants one
       definition. */
    void setValueNames (const string &commaList, bool fromFile = false);

    float *allocate (unsigned int elements);

    const string &nodePtrName (void) const { return nodePtrName_; }
    const string &argPtrName (void) const { return argPtrName_; }

    int nodePtrId (void) const { return nodePtrId_; }
    void setNodePtrId (int id) { nodePtrId_ = id; }

    int argPtrId (void) const { return argPtrId_; }
    void setArgPtrId (int id) { argPtrId_ = id; }

    thArg *argPtr (void) const { return argPtr_; }
    void setArgPtr (thArg *arg) { argPtr_ = arg; }

    float *values (void) const { return values_; }

    type_signal_arg_changed signal_arg_changed (void) {
        return m_signal_arg_changed;
    }

    /* In the near future we have to also implement some way of limiting the
       values...  like only ints, or only certain fractions (some things will
       probably use this, I suggest some kind of array of acceptable values,
       with some way of specifying ints only...)  But just default to all
       floats between argMin and argMax. */
    /* We will probably also want more things here later, but this was all I
       could think of NEEDING right now. */
    float operator[] (unsigned int i) const {
        /* empty */
        if (len_ == 0) 
        {
            return 0;
        }
        
        else if (len_ == 1)
        {
            return values_[0];
        }
        else if (i < len_) {
            return values_[i];
        }

        /* else */
        return values_[i%len_];
    }
protected:
    string nodePtrName_; /* name of the node a pointer points to */
    string argPtrName_;  /* name of the argument a pointer points to */
    int nodePtrId_;      /* index of the node to which the pointer points */
    int argPtrId_;       /* index of the arg to which the pointer points */
    thArg *argPtr_;      /* actual pointer to another arg- for midi chan and
                            note args */

    ArgType type_;       /* is this arg a value or a pointer? */
    string name_;        /* argument's name */
    int index_;          /* where in the arg index this arg is located */
    float *values_;      /* a pointer to an array of values */
    unsigned int len_;   /* number of elements in argValues */

    /* Okay, a bit more info about the data */
    float min_, max_;        /* for knobs and stuff, I'm sure it will be useful
                                elsewhere, too */
    WidgetType widgetType_;  /* our widget type */

    /* See step() and valueNames(). A vector rather than a count and a pointer
       because a thArg is copy-constructed once per note per arg, and the
       copy has to own its strings; empty is the overwhelming majority and
       costs no allocation. */
    float step_;
    vector<string> valueNames_;
    bool typedByFile_;

    string label_;
    string group_;           /* This will be displayed in the UI */
    string units_;           /* This will be displayed too. ms, Hz, sec etc.*/
    string comment_;

    type_signal_arg_changed m_signal_arg_changed;
};

#endif /* TH_ARG_H */
