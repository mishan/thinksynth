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

#ifndef THH_FORM_H
#define THH_FORM_H

#include <string>
#include <vector>

#include "thHidBlock.h"
#include "thhControls.h"

/* The configurator's form over a device's profile (docs/PHONE.md, "The
   configurator"), without a toolkit: what it shows, and its edits, each
   made to the player's profile text by thhProfileEdit. */

/* An action the form offers, by the word the language writes it with:
   whether a button or an axis takes it, and what it is of, if anything
   (semis, layer or arg). */
struct thhFormKind
{
    const char        *name;
    thHidBinding::Kind kind;
    bool               button, axis;
    const char        *param;
};

const std::vector<thhFormKind> &thhFormKinds ();
const char *thhFormKindName (thHidBinding::Kind kind);

struct thhFormControl
{
    std::string control;
    bool        axis;
    bool        yours;      /* the bindings are the player's, over its own */
    bool        off;        /* the player ignores its own, and has none */
    bool        own;        /* the device binds it itself */
    std::vector<thHidBinding> bindings;     /* what it does outside layers */
    std::vector<std::string>  layered;      /* `layer: statement' for each
                                               layer's binding of it */
};

struct thhForm
{
    std::vector<thhFormControl> controls;   /* the buttons, then the axes */
    std::vector<std::string>    layers;     /* the player's, to hold or toggle */
    std::string                 problem;    /* why the player's text doesn't
                                               read, or nothing */
};

/* The form for the device's own profile, `fallback', with the player's
   `text' over it. */
thhForm thhFormOf (const thhControls &controls, const char *fallback,
                   const std::string &text);

/* An edit of `control' in `text': `op' is set (its `which'th binding as
   the form shows them), add, remove (the `which'th), reset or off. A set
   or an add takes `fields', `key=value' lines applied in order to the
   binding, or to a new one: kind first, to start one of that kind afresh,
   then its param and options by the names thhTakes has; min and max
   empty for none; latch and both 1 or 0. Writing over the device's own
   bindings writes the ones kept as the player's. False, with why, and the
   text as it was, if the result wouldn't play. */
bool thhFormEdit (std::string &text, const thhControls &controls,
                  const char *fallback, const std::string &op,
                  const std::string &control, int which,
                  const std::string &fields, std::string &err);

#endif /* THH_FORM_H */
