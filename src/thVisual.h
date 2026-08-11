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

#ifndef TH_VISUAL_H
#define TH_VISUAL_H 1

#include <string>

#include <cairo.h>

/* For THINK_PLUGIN_API, which is what marks the six entry points a visual
   module exports. Nothing else here comes from libthink -- and that is the
   point: thVisual lives in src/ rather than in libthink/ so that cairo stays
   out of the engine and out of every headless harness that links it. The
   visual host is a GUI concern. */
#include "thExport.h"

/* Named explicitly rather than relying on think.h's `using namespace std'
   having been pulled in first, for the same reason NodeGraph.h does: this
   header has to be includable from a translation unit that knows nothing about
   the engine. */
using std::string;

#define VISUAL_IFACE_VER 1

/* An inline variable is emitted only if something odr-uses it, and nothing in
   a plugin ever mentions its own version byte -- so `inline' alone made the
   symbol vanish from meter.so and thVisual refused to load it. `used' is what
   says "emit this anyway". __declspec(dllexport) already implies it, so this
   is only needed on the ELF/Mach-O side. */
#if defined(_WIN32) || defined(__CYGWIN__)
#  define THINK_VISUAL_KEEP
#else
#  define THINK_VISUAL_KEEP __attribute__((used))
#endif

/* What a visual instance assumes if it is handed a sample rate of zero. A
   module has to have something to divide by, and every decay constant is
   per-sample, so this is not a nicety. */
#define TH_VISUAL_DEFAULT_RATE 44100

/* A visualizer: something that is fed samples and draws.
 *
 * A separate ABI from thPlugin's rather than an extension of it. A DSP
 * plugin's whole interface is module_callback(thNode *, thSynthTree *, ...) --
 * it is handed a node in a graph and expected to write an arg. A scope has no
 * node, writes no arg, and needs a drawing context and a history instead.
 * Overloading one onto the other would make every visualizer pretend to be a
 * graph node, which is exactly what §1 of VISUALIZERS.md decided against.
 *
 * These are loaded by the GUI, not by the parser. A .dsp cannot name one and
 * the engine never sees one.
 *
 *
 * WHICH THREAD
 *
 * All of it is the GUI thread. The probe ring is drained there, so feed() and
 * draw() both run there, and a visual plugin may allocate, keep history, use
 * the C++ library and take as long as a frame allows. None of the real-time
 * discipline that governs plugins/osc applies here.
 *
 * Saying that explicitly because the opposite habit is well established
 * everywhere else in this tree, and a visualizer written as though it were in
 * an audio callback would be needlessly crippled.
 *
 *
 * WHY FEED AND DRAW ARE SEPARATE
 *
 * A spectrogram has to see every sample to produce a continuous waterfall, but
 * it is drawn thirty times a second. Folding the two together would either
 * drop columns between frames or tie the drawing rate to the audio rate. So
 * feed() runs on every drain and draw() on the frame tick, and what
 * accumulating means is the plugin's business: a meter integrates, a scope
 * keeps a triggered slice, a spectrogram appends columns.
 *
 * draw() must be able to run without a feed() in between (nothing new arrived)
 * and feed() without a draw() (the window is hidden), any number of times.
 *
 *
 * WHY CAIRO
 *
 * The alternatives were a raw pixel buffer and a fixed data model -- curve,
 * columns, scalar -- drawn by the canvas. Both avoid a dependency here; both
 * also mean a visual plugin cannot produce a look the canvas did not
 * anticipate, which is most of the point of making these plugins at all. A
 * spectrogram wants a pixel buffer, a scope wants crisp lines, a meter wants
 * text; nothing short of a real drawing API covers the three.
 *
 * Cairo is a stable C ABI and is present wherever this builds, since gtkmm-4
 * pulls it in. The containment is that only plugins/visual links it: libthink
 * and the 62 DSP plugins are untouched. It also makes the plugins testable
 * with no display at all, by drawing to an image surface -- which is what
 * scripts/visualcheck does.
 */

class thVisual;

#ifdef VISUAL_PLUGIN_BUILD
extern "C" {
    /* The version byte thVisual::moduleLoad looks up before anything else.
     *
     * `inline' rather than a plain definition: this is a header, and a plugin
     * that ever grows past one translation unit would otherwise get a
     * multiple-definition error at link time for including it twice. C++17
     * inline variables are exactly the case, and the exported symbol name is
     * unchanged.
     *
     * Inside the extern "C" block for the same reason the functions are: the
     * host looks this up by its plain spelling. (thPlugin.h defines
     * `apiversion' the same way and has the same latent hazard; every one of
     * the 62 DSP plugins is a single .cpp, so it has never come up.) */
    THINK_PLUGIN_API THINK_VISUAL_KEEP inline unsigned char
        visual_apiversion = VISUAL_IFACE_VER;

    /* Once per module. Must call setName() and setDesc(); may call
       setPreferredSize(). Returns non-zero to refuse to load. */
    THINK_PLUGIN_API int visual_init (thVisual *visual);

    /* One instance per probe. Whatever this returns is opaque to the host and
       is handed back to feed, draw and close. NULL means "cannot". */
    THINK_PLUGIN_API void *visual_open (thVisual *visual,
                                        unsigned int samplerate);

    /* n samples of the probed signal, in order. May be called with n larger
       than any window, and may be called many times between draws. */
    THINK_PLUGIN_API int visual_feed (void *inst, const float *samples,
                                      unsigned int n);

    /* Draw the current state into the w x h box at the origin of `cr'. The
       host has already clipped to that box and has not otherwise transformed
       the context. w or h may be zero or one. */
    THINK_PLUGIN_API int visual_draw (void *inst, cairo_t *cr, int w, int h);

    THINK_PLUGIN_API void visual_close (void *inst);

    /* Once per module, at unload. Optional. */
    THINK_PLUGIN_API void visual_cleanup (thVisual *visual);
}
#endif /* VISUAL_PLUGIN_BUILD */

/* The host side of one loaded visual module.
 *
 * Deliberately shaped like thPlugin -- a path, a description, a dlopen handle
 * and a set of looked-up entry points -- so that there is one story in this
 * tree about what a plugin is.
 */
class thVisual {
public:
    thVisual (const string &path);
    ~thVisual (void);

    enum State { LOADED, NOTLOADED };

    typedef int   (*VisualInit)    (thVisual *);
    typedef void *(*VisualOpen)    (thVisual *, unsigned int);
    typedef int   (*VisualFeed)    (void *, const float *, unsigned int);
    typedef int   (*VisualDraw)    (void *, cairo_t *, int, int);
    typedef void  (*VisualClose)   (void *);
    typedef void  (*VisualCleanup) (thVisual *);

    const string &path (void) const { return path_; }

    /* What the palette shows. `name' is the short one -- "scope" -- and
       matches the file on disk; `desc' is the sentence. */
    const string &name (void) const { return name_; }
    const string &desc (void) const { return desc_; }

    State state (void) const { return state_; }

    /* Called from visual_init. */
    void setName (const string &name) { name_ = name; }
    void setDesc (const string &desc) { desc_ = desc; }

    /* What this wants to be drawn at, in unzoomed canvas pixels. The canvas is
       free to ignore the width -- a probe panel is as wide as the node it sits
       on -- but the height is worth respecting: a meter needs one row and a
       spectrogram needs a picture. */
    void setPreferredSize (int w, int h) { prefW_ = w; prefH_ = h; }

    int preferredWidth (void) const { return prefW_; }
    int preferredHeight (void) const { return prefH_; }

    /* ---- instances ---- */

    /* NULL if the module did not load or refuses. */
    void *open (unsigned int samplerate);
    void feed (void *inst, const float *samples, unsigned int n);
    void draw (void *inst, cairo_t *cr, int w, int h);
    void close (void *inst);

private:
    /* Copying would give two owners of one dlopen handle. */
    thVisual (const thVisual &);
    thVisual &operator= (const thVisual &);

    int moduleLoad (void);
    void moduleUnload (void);

    string path_;
    string name_;
    string desc_;

    State state_;
    void *handle_;

    int prefW_, prefH_;

    VisualOpen open_;
    VisualFeed feed_;
    VisualDraw draw_;
    VisualClose close_;
    VisualCleanup cleanup_;
};

#endif /* TH_VISUAL_H */
