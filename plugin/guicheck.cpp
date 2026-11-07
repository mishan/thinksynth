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

/*
 * guicheck -- the editor, opened by a host, is the panel.
 *
 *   guicheck build/bin/thinksynth-juno.clap
 *
 * uicheck holds Panel to its layout and arithmetic with no window at all;
 * this holds the window to Panel. It opens the editor through CLAP's gui
 * extension as a floating X11 window, at a scale of 1 and of 2, lets it
 * draw, reads its pixels back off the X server, and compares them with
 * Panel drawn straight into an image at the same scale. The two are drawn
 * by one cairo each and need not agree to the bit -- text is rasterized
 * per surface -- so what is asked is that nearly every pixel agrees
 * closely. A window drawn at the wrong scale, or not drawn, agrees on a
 * fraction of them. And it asks the size the editor gives before it
 * opens, which is what a host sizes its frame by; and drags a knob with
 * the X server's own test input, up by 40 of the panel's pixels -- 40
 * times the scale of the screen's, short of the knob's end so a mouse
 * read at the wrong scale lands elsewhere -- and asks the plugin where the
 * value ended up, and that the host was sent one gesture around it.
 *
 * Needs a display, which ctest gives it through scripts/headless.sh; says
 * SKIP and exits 77 without one. Otherwise the exit status is the number
 * of failures.
 */

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <cairo.h>

#include <string>
#include <vector>

#include "clap/entry.h"
#include "clap/ext/gui.h"
#include "clap/events.h"
#include "clap/ext/params.h"
#include "clap/process.h"
#include "clap/ext/timer-support.h"
#include "clap/host.h"
#include "clap/plugin-factory.h"
#include "clap/plugin.h"

#include "Controls.h"
#include "Panel.h"

#include "DistrhoPluginInfo.h"

static int failed = 0;

static void check (bool ok, const std::string &what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());

    if (!ok)
        failed++;
}

/* ---- the host: a gui to be resized and timers to drive the editor ---- */

static bool timerRegister (const clap_host_t *, uint32_t, clap_id *id)
{
    *id = 1;
    return true;
}

static bool timerUnregister (const clap_host_t *, clap_id)
{
    return true;
}

static const clap_host_timer_support_t timers = {
    timerRegister, timerUnregister,
};

static void guiHint (const clap_host_t *) { }
static bool guiResize (const clap_host_t *, uint32_t, uint32_t) { return true; }
static bool guiRequest (const clap_host_t *) { return true; }
static void guiClosed (const clap_host_t *, bool) { }

static const clap_host_gui_t hostGui = {
    guiHint, guiResize, guiRequest, guiRequest, guiClosed,
};

static const void *hostExtension (const clap_host_t *, const char *id)
{
    if (!strcmp(id, CLAP_EXT_GUI))
        return &hostGui;

    if (!strcmp(id, CLAP_EXT_TIMER_SUPPORT))
        return &timers;

    return NULL;
}

static void hostRequest (const clap_host_t *) { }

static const clap_host_t host = {
    CLAP_VERSION, NULL, "guicheck", "thinksynth", "", "0",
    hostExtension, hostRequest, hostRequest, hostRequest,
};

/* ---- the X side ------------------------------------------------------- */

/* The mapped top-level window of exactly w x h, or 0. */
static Window findWindow (Display *dpy, unsigned w, unsigned h)
{
    Window root = DefaultRootWindow(dpy), parent, *kids = NULL;
    unsigned n = 0;
    Window found = 0;

    if (!XQueryTree(dpy, root, &root, &parent, &kids, &n))
        return 0;

    for (unsigned i = 0; i < n && !found; i++)
    {
        XWindowAttributes a;

        if (XGetWindowAttributes(dpy, kids[i], &a) &&
            a.map_state == IsViewable && (unsigned)a.width == w &&
            (unsigned)a.height == h)
            found = kids[i];
    }

    if (kids)
        XFree(kids);

    return found;
}

/* The types of the parameter events the plugin sent the host. */
static std::vector<uint16_t> gestures;

static bool collect (const clap_output_events_t *,
                     const clap_event_header_t *e)
{
    if (e->space_id == CLAP_CORE_EVENT_SPACE_ID &&
        e->type >= CLAP_EVENT_PARAM_VALUE &&
        e->type <= CLAP_EVENT_PARAM_GESTURE_END)
        gestures.push_back(e->type);

    return true;
}

static uint32_t noEvents (const clap_input_events_t *)
{
    return 0;
}

static const clap_event_header_t *noEvent (const clap_input_events_t *,
                                           uint32_t)
{
    return NULL;
}

/* One block of 256 frames, into nowhere. */
static void processBlock (const clap_plugin_t *plugin)
{
    std::vector<float> left(256), right(256);
    float *channels[2] = { &left[0], &right[0] };
    clap_audio_buffer_t buffer;
    clap_input_events_t in = { NULL, noEvents, noEvent };
    clap_output_events_t out = { NULL, collect };
    clap_process_t process;

    memset(&buffer, 0, sizeof(buffer));
    buffer.data32 = channels;
    buffer.channel_count = 2;

    memset(&process, 0, sizeof(process));
    process.frames_count = 256;
    process.audio_outputs = &buffer;
    process.audio_outputs_count = 1;
    process.in_events = &in;
    process.out_events = &out;

    plugin->process(plugin, &process);
}

/* Runs the editor's timer for `ms', which is when it reads X's events. */
static void pump (const clap_plugin_t *plugin,
                  const clap_plugin_timer_support_t *timer, Display *dpy,
                  int ms)
{
    for (int i = 0; i < ms / 10; i++)
    {
        XSync(dpy, False);

        if (timer)
            timer->on_timer(plugin, 1);

        usleep(10000);
    }
}

/* The fraction of pixels whose red, green and blue are each within 32 of
   the reference's. */
static double agreement (XImage *img, cairo_surface_t *ref)
{
    cairo_surface_flush(ref);

    const unsigned char *d = cairo_image_surface_get_data(ref);
    const int stride = cairo_image_surface_get_stride(ref);
    const int w = cairo_image_surface_get_width(ref);
    const int h = cairo_image_surface_get_height(ref);
    long close = 0;

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const unsigned long p = XGetPixel(img, x, y);
            uint32_t q;

            memcpy(&q, d + y * stride + x * 4, 4);

            bool near = true;

            for (int shift = 0; shift <= 16 && near; shift += 8)
                near = abs((int)((p >> shift) & 0xff) -
                           (int)((q >> shift) & 0xff)) <= 32;

            if (near)
                close++;
        }

    return (double)close / ((double)w * h);
}

int main (int argc, char **argv)
{
    if (argc != 2)
    {
        printf("usage: %s PLUGIN.clap\n", argv[0]);
        return 2;
    }

    Display *dpy = XOpenDisplay(NULL);

    if (dpy == NULL)
    {
        printf("SKIP  no display\n");
        return 77;
    }

    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    const clap_plugin_entry_t *entry = module ? (const clap_plugin_entry_t *)
        dlsym(module, "clap_entry") : NULL;

    if (entry == NULL || !entry->init(argv[1]))
    {
        printf("FAIL  %s would not load\n", argv[1]);
        return 2;
    }

    const clap_plugin_factory_t *factory = (const clap_plugin_factory_t *)
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    const char *id = factory->get_plugin_descriptor(factory, 0)->id;

    const Panel panel(controls(), DISTRHO_PLUGIN_NAME, controlsDescription());

    check(DISTRHO_UI_DEFAULT_WIDTH == panel.width() &&
          DISTRHO_UI_DEFAULT_HEIGHT == panel.height(),
          "the default size DPF is given is the panel's: " +
          std::to_string(panel.width()) + " x " +
          std::to_string(panel.height()));

    for (int scale = 1; scale <= 2; scale++)
    {
        const std::string at = " at " + std::to_string(scale) + "x";
        const clap_plugin_t *plugin =
            factory->create_plugin(factory, &host, id);

        plugin->init(plugin);
        plugin->activate(plugin, 48000, 1, 4096);
        plugin->start_processing(plugin);

        const clap_plugin_gui_t *gui = (const clap_plugin_gui_t *)
            plugin->get_extension(plugin, CLAP_EXT_GUI);
        const clap_plugin_timer_support_t *timer =
            (const clap_plugin_timer_support_t *)
            plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT);

        uint32_t w = 0, h = 0;
        const bool opened = gui != NULL &&
                            gui->create(plugin, CLAP_WINDOW_API_X11, true);

        if (opened)
        {
            gui->set_scale(plugin, scale);
            gui->get_size(plugin, &w, &h);
        }

        check(opened && w == (uint32_t)(panel.width() * scale) &&
              h == (uint32_t)(panel.height() * scale),
              "the editor opens" + at + ", " + std::to_string(w) + " x " +
              std::to_string(h));

        if (!opened)
        {
            plugin->destroy(plugin);
            continue;
        }

        gui->show(plugin);

        /* Let it map and draw: the editor runs on the host's timer. */
        Window win = 0;

        for (int i = 0; i < 100; i++)
        {
            if (timer)
                timer->on_timer(plugin, 1);

            usleep(10000);

            if (!win)
                win = findWindow(dpy, w, h);
        }

        XImage *img = win ? XGetImage(dpy, win, 0, 0, w, h, AllPlanes,
                                      ZPixmap) : NULL;

        cairo_surface_t *ref = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, (int)w, (int)h);
        cairo_t *cr = cairo_create(ref);
        std::vector<float> values;

        for (size_t i = 0; i < controls().size(); i++)
            values.push_back(controls()[i].def);

        cairo_scale(cr, scale, scale);
        panel.draw(cr, values, -1, -1);

        const double agree = img ? agreement(img, ref) : 0;
        char pct[32];

        snprintf(pct, sizeof(pct), "%.1f%%", agree * 100);

        check(img != NULL && agree > 0.97,
              "and draws the panel" + at + ": " + pct +
              " of pixels agree");

        /* GUICHECK_DUMP=dir writes what was read and what was expected,
           for a failure to be looked at. */
        if (img && getenv("GUICHECK_DUMP"))
        {
            cairo_surface_t *got = cairo_image_surface_create(
                CAIRO_FORMAT_RGB24, (int)w, (int)h);

            cairo_surface_flush(got);

            for (uint32_t y = 0; y < h; y++)
                for (uint32_t x = 0; x < w; x++)
                {
                    const uint32_t p = (uint32_t)XGetPixel(img, x, y);

                    memcpy(cairo_image_surface_get_data(got) +
                           y * cairo_image_surface_get_stride(got) + x * 4,
                           &p, 4);
                }

            cairo_surface_mark_dirty(got);

            const std::string dir = getenv("GUICHECK_DUMP");

            cairo_surface_write_to_png(got, (dir + "/window" +
                std::to_string(scale) + "x.png").c_str());
            cairo_surface_write_to_png(ref, (dir + "/panel" +
                std::to_string(scale) + "x.png").c_str());
            cairo_surface_destroy(got);
        }

        if (img)
            XDestroyImage(img);

        /* A drag on Resonance's knob. */
        int res = -1;

        for (size_t i = 0; i < controls().size(); i++)
            if (controls()[i].name == "res")
                res = (int)i;

        const clap_plugin_params_t *params = (const clap_plugin_params_t *)
            plugin->get_extension(plugin, CLAP_EXT_PARAMS);
        XWindowAttributes wa;
        double kx = 0, ky = 0;
        int ev, err, major, minor;

        if (win && res >= 0 && params && panel.center(res, kx, ky) &&
            XGetWindowAttributes(dpy, win, &wa) &&
            XTestQueryExtension(dpy, &ev, &err, &major, &minor))
        {
            const int x = wa.x + (int)(kx * scale);
            const int y = wa.y + (int)(ky * scale);

            XTestFakeMotionEvent(dpy, -1, x, y, CurrentTime);
            pump(plugin, timer, dpy, 100);
            XTestFakeButtonEvent(dpy, 1, True, CurrentTime);
            pump(plugin, timer, dpy, 100);

            for (int step = 1; step <= 4; step++)
            {
                XTestFakeMotionEvent(dpy, -1, x, y - 10 * step * scale,
                                     CurrentTime);
                pump(plugin, timer, dpy, 50);
            }

            XTestFakeButtonEvent(dpy, 1, False, CurrentTime);
            pump(plugin, timer, dpy, 100);

            double v = -1;
            const float want = panel.drag(res, controls()[res].def, 40,
                                          false);

            /* A block processed, which is when DPF hands the editor's
               changes to the plugin, and hands the host what they were as
               output events: a gesture begun, values, a gesture ended. */
            gestures.clear();
            processBlock(plugin);

            params->get_value(plugin, (clap_id)res, &v);

            if (getenv("GUICHECK_DUMP"))
            {
                XImage *after = XGetImage(dpy, win, 0, 0, w, h, AllPlanes,
                                          ZPixmap);
                cairo_surface_t *got = cairo_image_surface_create(
                    CAIRO_FORMAT_RGB24, (int)w, (int)h);

                cairo_surface_flush(got);

                for (uint32_t yy = 0; after && yy < h; yy++)
                    for (uint32_t xx = 0; xx < w; xx++)
                    {
                        const uint32_t p = (uint32_t)XGetPixel(after, xx, yy);

                        memcpy(cairo_image_surface_get_data(got) +
                               yy * cairo_image_surface_get_stride(got) +
                               xx * 4, &p, 4);
                    }

                cairo_surface_mark_dirty(got);
                cairo_surface_write_to_png(got, (std::string(
                    getenv("GUICHECK_DUMP")) + "/dragged" +
                    std::to_string(scale) + "x.png").c_str());
                cairo_surface_destroy(got);

                if (after)
                    XDestroyImage(after);
            }

            char what[128];

            snprintf(what, sizeof(what),
                     "a drag of 40 panel pixels up turns Resonance from "
                     "%.2f to %.2f%s: %.3f", controls()[res].def, want,
                     at.c_str(), v);

            check(fabs(v - want) < 1e-4, what);

            /* One gesture around the values, and nothing after its end. */
            bool paired = gestures.size() >= 3 &&
                          gestures.front() == CLAP_EVENT_PARAM_GESTURE_BEGIN &&
                          gestures.back() == CLAP_EVENT_PARAM_GESTURE_END;

            for (size_t k = 1; paired && k + 1 < gestures.size(); k++)
                paired = gestures[k] == CLAP_EVENT_PARAM_VALUE;

            check(paired, "as one gesture: begun, " +
                          std::to_string(gestures.size() > 2 ?
                                         gestures.size() - 2 : 0) +
                          " values, ended" + at);
        }
        else
            check(false, "a knob to drag" + at);

        cairo_destroy(cr);
        cairo_surface_destroy(ref);

        gui->hide(plugin);
        gui->destroy(plugin);
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    entry->deinit();
    XCloseDisplay(dpy);

    printf("\n%d failure(s)\n", failed);

    return failed;
}
