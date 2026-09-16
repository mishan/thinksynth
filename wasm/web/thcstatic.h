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
 * Everything a composer plugin includes, included once, at file scope,
 * ahead of the plugin. thinkstatic.h, for the other ABI.
 *
 * The composers are compiled into the one module the same way the DSP
 * plugins are, each inside a namespace of its own, so a header first seen
 * inside that namespace would drag std:: in with it. Included here first,
 * every one is behind its guard by the time the plugin's own #include
 * lines are reached.
 *
 * The one difference from thinkstatic.h is that a namespace is not enough
 * on its own here. A composer's exports are `extern "C"', which is a
 * linkage and not a scope: sixteen namespaces would still be sixteen
 * definitions of one C symbol. So the wrapper cmake/ThinkPlugin.cmake
 * writes renames each export with a #define before the include, and the
 * generated table looks the plugin's entry points up under those names.
 * Nothing in the plugin sees the difference; each still spells its own
 * exports composer_init, composer_tick and the rest.
 *
 * COMPOSER_PLUGIN_BUILD is deliberately not defined. It declares the
 * exports and defines the version byte, and a rename would have to reach
 * those declarations too; the definitions in the plugin stand on their
 * own, and the version byte a compiled-in composer is gated on is the one
 * the table carries.
 */

#ifndef TH_WEB_STATIC_COMPOSER_H
#define TH_WEB_STATIC_COMPOSER_H 1

#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "thcomposer.h"
#include "thcRandom.h"
#include "thMath.h"

#endif /* TH_WEB_STATIC_COMPOSER_H */
