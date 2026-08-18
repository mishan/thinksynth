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

#include "config.h"

#include <stdio.h>

#include <filesystem>

#include "thcPlugin.h"
#include "libthink/thDynLib.h"

thcPlugin::thcPlugin (const string &path)
    : path_(path), state_(NOTLOADED), handle_(NULL), flags_(0),
      create_(NULL), tick_(NULL), receive_(NULL), paramChanged_(NULL),
      input_(NULL), capture_(NULL),
      draw_(NULL), destroy_(NULL)
{
    /* The name a chain will refer to this by is the filename with the
       suffix off -- "eno_line", not ".../composer/eno_line.so". The ABI
       has no set_name on purpose: a module whose internal name disagreed
       with its filename would be findable by one spelling and not the
       other, which is the exact confusion thVisual's setName produced a
       guard against. */
    name_ = std::filesystem::path(path).stem().string();

    if (moduleLoad() == 0)
        state_ = LOADED;
    else
        moduleUnload();
}

thcPlugin::~thcPlugin (void)
{
    moduleUnload();
}

int
thcPlugin::moduleLoad (void)
{
    handle_ = thDynLib::open(path_);

    if (handle_ == NULL)
    {
        fprintf(stderr, "thcPlugin: %s: %s\n", path_.c_str(),
                thDynLib::lastError().c_str());
        return -1;
    }

    /* The version byte first, before calling anything -- same gate as
       thPlugin and thVisual, same reasoning: a layout mismatch fails
       loudly here instead of corrupting memory later. */
    const unsigned char *ver = (const unsigned char *)
        thDynLib::symbol(handle_, "composer_apiversion");

    if (ver == NULL)
    {
        fprintf(stderr, "thcPlugin: %s exports no composer_apiversion; "
                "not a composer module\n", path_.c_str());
        return -1;
    }

    if (*ver != COMPOSER_IFACE_VER)
    {
        fprintf(stderr, "thcPlugin: %s is interface version %d, "
                "host wants %d\n", path_.c_str(), (int)*ver,
                COMPOSER_IFACE_VER);
        return -1;
    }

    ComposerInit init = (ComposerInit)
        thDynLib::symbol(handle_, "composer_init");

    create_  = (ComposerCreate)  thDynLib::symbol(handle_, "composer_create");
    destroy_ = (ComposerDestroy) thDynLib::symbol(handle_, "composer_destroy");

    if (init == NULL || create_ == NULL || destroy_ == NULL)
    {
        fprintf(stderr, "thcPlugin: %s is missing a mandatory entry point "
                "(composer_init/create/destroy)\n", path_.c_str());
        return -1;
    }

    /* The optional ones. Absence is a statement of role, checked below. */
    tick_ = (ComposerTick) thDynLib::symbol(handle_, "composer_tick");
    receive_ = (ComposerReceive) thDynLib::symbol(handle_, "composer_receive");
    paramChanged_ = (ComposerParamChanged)
        thDynLib::symbol(handle_, "composer_param_changed");
    draw_ = (ComposerDraw) thDynLib::symbol(handle_, "composer_draw");

    /* Both optional, and looked up the same way everything optional here
       is: absent means the feature is simply not offered, which is what
       lets an old module keep loading against a newer host. */
    input_ = (ComposerInput) thDynLib::symbol(handle_, "composer_input");
    capture_ = (ComposerCapture)
        thDynLib::symbol(handle_, "composer_capture");

    thcComposerInfo info;
    info.host = this;
    info.register_param = hostRegisterParam;
    info.set_flags = hostSetFlags;
    info.set_desc = hostSetDesc;

    if (init(&info) != 0)
    {
        fprintf(stderr, "thcPlugin: %s: composer_init refused\n",
                path_.c_str());
        return -1;
    }

    /* Role versus exports, both directions. A declared generator with no
       tick is a stage that never runs; an exported tick on a module that
       declares no generator role would never be scheduled. Either is a
       bug in the module and this is the only moment anyone will look. */
    if (isGenerator() && tick_ == NULL)
    {
        fprintf(stderr, "thcPlugin: %s declares THC_GENERATOR but exports "
                "no composer_tick\n", path_.c_str());
        return -1;
    }

    if (isTransformer() && receive_ == NULL)
    {
        fprintf(stderr, "thcPlugin: %s declares THC_TRANSFORMER but exports "
                "no composer_receive\n", path_.c_str());
        return -1;
    }

    /* And the mirror image: an exported entry point whose role was
       never declared would still answer hasTick()/hasReceive() and get
       scheduled or fed on the quiet. The flags are the module's public
       statement of what it is; an export that contradicts them is the
       same bug wearing the other shirt. */
    if (tick_ != NULL && !isGenerator())
    {
        fprintf(stderr, "thcPlugin: %s exports composer_tick but never "
                "declared THC_GENERATOR\n", path_.c_str());
        return -1;
    }

    if (receive_ != NULL && !isTransformer())
    {
        fprintf(stderr, "thcPlugin: %s exports composer_receive but never "
                "declared THC_TRANSFORMER\n", path_.c_str());
        return -1;
    }

    if (flags_ == 0)
    {
        fprintf(stderr, "thcPlugin: %s declared no role "
                "(set_flags was not called)\n", path_.c_str());
        return -1;
    }

    /* And no roles this interface version has never heard of: a module
       relying on a flag bit the host does not implement should find
       out here, not by whatever the unknown bit silently fails to do. */
    if ((flags_ & ~(THC_GENERATOR | THC_TRANSFORMER)) != 0)
    {
        fprintf(stderr, "thcPlugin: %s declared flags 0x%x, which this "
                "interface version does not define\n", path_.c_str(),
                flags_);
        return -1;
    }

    return 0;
}

void
thcPlugin::moduleUnload (void)
{
    if (handle_ != NULL)
    {
        thDynLib::close(handle_);
        handle_ = NULL;
    }

    state_ = NOTLOADED;
    create_ = NULL;
    tick_ = NULL;
    receive_ = NULL;
    paramChanged_ = NULL;
    draw_ = NULL;
    input_ = NULL;
    capture_ = NULL;
    destroy_ = NULL;

    /* NOTLOADED means empty, not "whatever a failed init left behind":
       a caller asking a dead module for its flags or params must get
       nothing, or isGenerator() keeps answering for a module that is
       not there. */
    flags_ = 0;
    desc_.clear();
    params_.clear();
}

/* ---- the host callbacks composer_init reaches us through ------------- */

int
thcPlugin::hostRegisterParam (void *host, const thcParamDef *def)
{
    thcPlugin *self = static_cast<thcPlugin *>(host);

    /* A dlopen'd module is effectively untrusted; a NULL def is its
       bug, reported as such rather than as the host's crash. */
    if (def == NULL)
    {
        fprintf(stderr, "thcPlugin: %s registered a NULL param\n",
                self->path_.c_str());
        return -1;
    }

    /* Copy everything: the module's strings are its own .rodata and the
       defs have to outlive this call by the life of the plugin. */
    ParamInfo p;
    p.name = def->name ? def->name : "";
    p.desc = def->desc ? def->desc : "";
    p.type = def->type;
    p.min = def->min;
    p.max = def->max;
    p.def = def->def;
    p.defString = def->def_string ? def->def_string : "";
    p.units = def->units ? def->units : "";

    self->params_.push_back(p);

    return (int)self->params_.size() - 1;
}

void
thcPlugin::hostSetFlags (void *host, int flags)
{
    static_cast<thcPlugin *>(host)->flags_ = flags;
}

void
thcPlugin::hostSetDesc (void *host, const char *description)
{
    static_cast<thcPlugin *>(host)->desc_ = description ? description : "";
}

/* ---------------------------------------------------------------------- */

int
thcPlugin::paramIndex (const string &name) const
{
    for (size_t i = 0; i < params_.size(); i++)
        if (params_[i].name == name)
            return (int)i;

    return -1;
}

void *
thcPlugin::create (const thcParams *params)
{
    if (state_ != LOADED || create_ == NULL || params == NULL)
        return NULL;

    return create_(params);
}

void
thcPlugin::destroy (void *state)
{
    if (state != NULL && destroy_ != NULL)
        destroy_(state);
}

double
thcPlugin::tick (void *state, const thcTransport *t, thcEventSink *out)
{
    /* NULL guards on every instance call: handing a module a state it
       never made is the host's mistake, and the module cannot be asked
       to survive it. */
    if (tick_ == NULL || state == NULL)
        return THC_NEVER;

    return tick_(state, t, out);
}

void
thcPlugin::receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    if (receive_ != NULL && state != NULL && ev != NULL)
        receive_(state, ev, out);
}

void
thcPlugin::paramChanged (void *state, int index)
{
    if (paramChanged_ != NULL && state != NULL)
        paramChanged_(state, index);
}

void
thcPlugin::draw (void *state, cairo_t *cr, double w, double h)
{
    if (draw_ != NULL && state != NULL && cr != NULL)
        draw_(state, cr, w, h);
}

void
thcPlugin::input (void *state, const thcInputEvent *ev)
{
    if (input_ != NULL && state != NULL && ev != NULL)
        input_(state, ev);
}

string
thcPlugin::capture (void *state, int index)
{
    if (capture_ == NULL || state == NULL)
        return "";

    /* Copied here, at the boundary, because the ABI promises the
       pointer only until the next call -- and every caller of this
       wants to keep the text long enough to splice it into a file. */
    const char *text = capture_(state, index);

    return text != NULL ? string(text) : string();
}
