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

/* What DPF asks of a plugin at compile time: its names and identities in
 * each format, and its shape. One instrument, MIDI in, stereo out, and an
 * editor drawn with cairo (ThinkUI.cpp).
 *
 * The identities are forever once a host has saved a project with the
 * plugin in it: the LV2 URI, the CLAP id and the VST3 unique ID are what a
 * project names the plugin by. The URI has no fragment because DPF makes
 * the editor's from it by appending one. */

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "thinksynth"
#define DISTRHO_PLUGIN_NAME    "thinksynth Juno"
#define DISTRHO_PLUGIN_URI     "https://github.com/mishan/thinksynth/juno"
#define DISTRHO_PLUGIN_CLAP_ID "org.thinksynth.juno"

#define DISTRHO_PLUGIN_BRAND_ID  Thnk
#define DISTRHO_PLUGIN_UNIQUE_ID TsJu

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_UI_USE_CAIRO            1
#define DISTRHO_UI_USER_RESIZABLE       0

/* What a host that asks before the editor exists is told; the editor then
   sizes itself to its panel (Panel.h). */
#define DISTRHO_UI_DEFAULT_WIDTH        880
#define DISTRHO_UI_DEFAULT_HEIGHT       340
#define DISTRHO_PLUGIN_IS_SYNTH         1
#define DISTRHO_PLUGIN_NUM_INPUTS       0
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_LATENCY     1

/* Not real-time safe, and saying so: a note-on copies a voice's graph when
   the channel's voice pool has none spare, which allocates. */
#define DISTRHO_PLUGIN_IS_RT_SAFE       0

#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth|Stereo"
#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:InstrumentPlugin"

#endif /* DISTRHO_PLUGIN_INFO_H_INCLUDED */
