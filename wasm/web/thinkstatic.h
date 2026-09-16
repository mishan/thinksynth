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

/*
 * Everything a DSP plugin includes, included once, at file scope, ahead of
 * the plugin.
 *
 * The browser build links every plugin into one module, and every plugin
 * defines the same names -- module_init, module_callback, module_cleanup,
 * args[], mystate -- so each is compiled inside a namespace of its own (see
 * cmake/ThinkPlugin.cmake here). A header included for the first time inside
 * that namespace would drag std:: and libthink in with it. Included here
 * first, every one of them is behind its guard by the time the plugin's own
 * #include lines are reached, and those lines do nothing.
 *
 * The list is every header a built DSP plugin includes. A plugin that grows
 * a new one fails to compile here, loudly and in the header it added, and
 * the fix is a line below.
 */

#ifndef TH_WEB_STATIC_H
#define TH_WEB_STATIC_H 1

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <atomic>

#include "think.h"

#include "thArg.h"
#include "thNode.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thSynth.h"
#include "thSynthTree.h"

#endif /* TH_WEB_STATIC_H */
