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

#include <stdio.h>

#include "thVisual.h"
#include "thDynLib.h"

thVisual::thVisual (const string &path)
    : path_(path), state_(NOTLOADED), handle_(NULL),
      prefW_(128), prefH_(64),
      open_(NULL), feed_(NULL), draw_(NULL), close_(NULL), cleanup_(NULL)
{
    if (moduleLoad() == 0)
        state_ = LOADED;
}

thVisual::~thVisual (void)
{
    moduleUnload();
}

/* Same shape as thPlugin::moduleLoad, including the single error exit that
   closes the handle. Every failure below the open used to leak the handle
   there, which on Windows also keeps the file locked; there is no reason to
   reintroduce that here. */
int thVisual::moduleLoad (void)
{
    VisualInit init;
    unsigned char *apiversion;

    handle_ = thDynLib::open(path_);

    if (handle_ == NULL)
    {
        fprintf(stderr, "thVisual: %s: %s\n", path_.c_str(),
                thDynLib::lastError().c_str());
        goto loaderr;
    }

    apiversion = (unsigned char *)thDynLib::symbol(handle_,
                                                   "visual_apiversion");

    if (apiversion == NULL)
    {
        fprintf(stderr, "thVisual: %s: no visual_apiversion symbol -- is this "
                "a visual plugin?\n", path_.c_str());
        goto loaderr;
    }

    if (*apiversion != VISUAL_IFACE_VER)
    {
        fprintf(stderr, "thVisual: %s: built against visual API v%d, this is "
                "v%d\n", path_.c_str(), (int)*apiversion, VISUAL_IFACE_VER);
        goto loaderr;
    }

    init = (VisualInit)thDynLib::symbol(handle_, "visual_init");
    open_ = (VisualOpen)thDynLib::symbol(handle_, "visual_open");
    feed_ = (VisualFeed)thDynLib::symbol(handle_, "visual_feed");
    draw_ = (VisualDraw)thDynLib::symbol(handle_, "visual_draw");
    close_ = (VisualClose)thDynLib::symbol(handle_, "visual_close");

    /* Optional; the others are not. */
    cleanup_ = (VisualCleanup)thDynLib::symbol(handle_, "visual_cleanup");

    if (init == NULL || open_ == NULL || feed_ == NULL || draw_ == NULL ||
        close_ == NULL)
    {
        fprintf(stderr, "thVisual: %s: missing one of visual_init, "
                "visual_open, visual_feed, visual_draw, visual_close\n",
                path_.c_str());
        goto loaderr;
    }

    if (init(this) != 0)
    {
        fprintf(stderr, "thVisual: %s: visual_init refused\n", path_.c_str());
        goto loaderr;
    }

    /* A module that names itself nothing cannot be offered in a palette, and
       finding that out at load time is better than drawing a blank row. */
    if (name_.empty())
    {
        fprintf(stderr, "thVisual: %s: visual_init did not set a name\n",
                path_.c_str());
        goto loaderr;
    }

    return 0;

loaderr:
    if (handle_ != NULL)
    {
        thDynLib::close(handle_);
        handle_ = NULL;
    }

    open_ = NULL;
    feed_ = NULL;
    draw_ = NULL;
    close_ = NULL;
    cleanup_ = NULL;

    state_ = NOTLOADED;

    return 1;
}

void thVisual::moduleUnload (void)
{
    if (handle_ == NULL)
        return;

    if (cleanup_)
        cleanup_(this);

    thDynLib::close(handle_);

    handle_ = NULL;
    state_ = NOTLOADED;
}

void *thVisual::open (unsigned int samplerate)
{
    if (state_ != LOADED || open_ == NULL)
        return NULL;

    return open_(this, samplerate);
}

void thVisual::feed (void *inst, const float *samples, unsigned int n)
{
    if (inst == NULL || feed_ == NULL || samples == NULL || n == 0)
        return;

    feed_(inst, samples, n);
}

void thVisual::draw (void *inst, cairo_t *cr, int w, int h)
{
    if (inst == NULL || draw_ == NULL || cr == NULL)
        return;

    /* A zero-sized box is not an error -- a collapsed panel or a canvas mid
       resize produces one -- but handing it to every plugin means every plugin
       has to remember to divide carefully. Refusing here is one check instead
       of one per module. */
    if (w <= 0 || h <= 0)
        return;

    /* The plugin draws inside the box and nowhere else. Doing this here rather
       than trusting each module is the difference between a bug in a
       visualizer being a bug in that visualizer and it being a smear across
       the whole canvas. save/restore so a plugin that leaves the context
       dirty -- a set source, a scaled matrix, an unfinished path -- cannot
       affect what is drawn after it. */
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_clip(cr);

    draw_(inst, cr, w, h);

    cairo_restore(cr);
}

void thVisual::close (void *inst)
{
    if (inst == NULL || close_ == NULL)
        return;

    close_(inst);
}
