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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gtkmm.h>
#include <gtk/gtk.h>

#include "think.h"

#include "Keyboard.h"

/* XXX: implement sticky keys??? */

/* conversion table for SHIFT chars */
static char    key_conv_in[] =  "~!@#$%^&*()_+|{}:\"<>?";
static char    key_conv_out[] = "`1234567890-=\\[];\',./";
/* list of keys */
static char    keylist_1[] = "1q2w3e4r5t6y7u8i9o0p-[=]\\";
static char    keylist_2[] = "azsxdcfvgbhnjmk,l.;/\'";

static unsigned int    color0 = 0x00000000;    /* key border                */
static unsigned int    color1 = 0x00FFFFFF;    /* white key                */
static unsigned int    color2 = 0x00000000;    /* black key                */
static unsigned int    color3 = 0x00C0FFFF;    /* middle C key            */
static unsigned int color4 = 0x005050FF;    /* white key / active       */
static unsigned int    color5 = 0x005050FF;    /* black key / active        */
/* 00707070 */ /* 00909090 */
static unsigned int    color6 = 0x005050FF;    /* middle C key / active    */
/* 0090D0D0 */

/* White keys in the 128-note range the widget covers: 10 full octaves of 7
   plus C..G of the eleventh. */
#define KB_WHITE_KEYS 75

static int key_sizes[5][7] =
{
    /* keyboard size 0: 450x45 pixels */
    {
        26,        /* black key height                    */
        19,        /* total height - black key height    */
        6,        /* white key total width            */
        3,        /* black key width                    */
        4,        /* white key width 1 (C, F)            */
        3,        /* white key width 2 (D, G, A)        */
        4        /* white key width 3 (E, B)            */
    },
    /* keyboard size 1: 600x60 pixels */
    {
        35,        /* black key height                    */
        25,        /* total height - black key height    */
        8,        /* white key total width            */
        5,        /* black key width                    */
        5,        /* white key width 1 (C, F)            */
        3,        /* white key width 2 (D, G, A)        */
        5        /* white key width 3 (E, B)            */
    },
    /* keyboard size 2: 750x75 pixels */
    {
        43,        /* black key height                    */
        32,        /* total height - black key height    */
        10,        /* white key total width            */
        6,        /* black key width                    */
        7,        /* white key width 1 (C, F)            */
        4,        /* white key width 2 (D, G, A)        */
        6        /* white key width 3 (E, B)            */
    },
    /* keyboard size 3: 1050x105 pixels */
    {
        61,        /* black key height                    */
        44,        /* total height - black key height    */
        14,        /* white key total width            */
        8,        /* black key width                    */
        10,        /* white key width 1 (C, F)            */
        6,        /* white key width 2 (D, G, A)        */
        9        /* white key width 3 (E, B)            */
    },
    /* keyboard size 4 */
    {
        43,        /* black key height                    */
        32,        /* total height - black key height    */
        11,        /* white key total width            */
        8,        /* black key width                    */
        7,        /* white key width 1 (C, F)            */
        3,        /* white key width 2 (D, G, A)        */
        6        /* white key width 3 (E, B)            */
    },


};

Keyboard::Keyboard (void)
{
    channel_ = 0;
    transpose_ = 0;
    
    /* keyboard parameters */
    veloc0_ = 32;
    veloc1_ = 64;
    veloc2_ = 96;
    veloc3_ = 127;
    mouse_notnum_ = -1;
    mouse_veloc_ = 127;
    cur_size_ = 4; /* keyboard widget size parameter */

    scaleX_ = scaleY_ = 1.0;

    img_height_ = key_sizes[cur_size_][0] + key_sizes[cur_size_][1];
    img_width_  = key_sizes[cur_size_][2] * KB_WHITE_KEYS;

    set_size_request(img_width_, img_height_);

    /* A DrawingArea is told what to draw with rather than overriding a draw
       vfunc, and it is told the size it has been given instead of reading its
       own allocation. */
    set_draw_func(sigc::mem_fun(*this, &Keyboard::onDraw));

    /* One controller per kind of input, each attached to this widget. There
       is no event mask any more -- a controller receives what it is for. */
    click_ = Gtk::GestureClick::create();

    /* Every button, not just the first: which one was pressed is what picks
       the velocity. */
    click_->set_button(0);
    click_->signal_pressed().connect(
        sigc::mem_fun(*this, &Keyboard::onPressed));
    click_->signal_released().connect(
        sigc::mem_fun(*this, &Keyboard::onReleased));
    add_controller(click_);

    keys_ = Gtk::EventControllerKey::create();
    keys_->signal_key_pressed().connect(
        sigc::mem_fun(*this, &Keyboard::onKeyPressed), false);
    keys_->signal_key_released().connect(
        sigc::mem_fun(*this, &Keyboard::onKeyReleased));
    add_controller(keys_);

    motion_ = Gtk::EventControllerMotion::create();
    motion_->signal_motion().connect(
        sigc::mem_fun(*this, &Keyboard::onMotion));
    add_controller(motion_);

    focus_ = Gtk::EventControllerFocus::create();
    focus_->signal_enter().connect(
        sigc::mem_fun(*this, &Keyboard::onFocusEnter));
    focus_->signal_leave().connect(
        sigc::mem_fun(*this, &Keyboard::onFocusLeave));
    add_controller(focus_);

    /* allow the widget to grab focus and process keypress events */
    set_focusable(true);

    focus_box_ = false;

    /* clear previous key state */
    for (int i = 0; i < 128; i++)
    {
        active_keys_[i] = 0;
        active_veloc_[i] = 0;
    }

    dispatchRedraw_.connect(
        sigc::mem_fun(*this, &Keyboard::queueRedraw));
}

void Keyboard::resetKeys (void)
{
    /* clear previous key state */
    for (int i = 0; i < 128; i++)
    {
        
        if (active_keys_[i] == 1)
            m_signal_note_off_(channel_, i);
        
        active_keys_[i] = 0;
        active_veloc_[i] = 0;
    }

    queue_draw();
}
  
Keyboard::~Keyboard (void)
{
}

/* Releases every note the keyboard is currently holding.
 *
 * Anything that changes how a keypress maps to a note has to do this first.
 * The note-off carries the number the note was *started* with, which is what
 * active_keys_ is indexed by -- releasing after the mapping has changed sends
 * note-off for a note that was never started, and leaves the original
 * sounding forever.
 */
void Keyboard::releaseAllNotes (void)
{
    mouse_notnum_ = -1;

    for (int i = 0; i < 128; i++)
    {
        if (active_keys_[i])
        {
            m_signal_note_off_(channel_, i);
        }

        active_keys_[i] = 0;
        active_veloc_[i] = 0;
    }
}

void Keyboard::SetChannel (int channel)
{
    /* turn off notes from the previous channel */
    releaseAllNotes();

    channel_ = channel;

    m_signal_channel_changed_(channel_);

    queue_draw();
}

void Keyboard::SetTranspose (int transpose)
{
    if (transpose < -72)
        transpose = -72;

    if (transpose > 72)
        transpose = 72;

    if (transpose == transpose_)
        return;

    /* Held notes follow the transpose rather than being dropped.
     *
     * keyval_to_notnum() adds transpose_, so a key held across a change would
     * otherwise release the wrong note and leave the original hanging. Simply
     * releasing everything is not right either: the physical keys are still
     * down, and X only auto-repeats the most recently pressed one, so a
     * three-note chord came back as a single note.
     *
     * The new number is just the old one shifted by the delta -- the mapping
     * is 48 + transpose_ + 12*octave + degree, so the key's contribution is
     * unchanged and only the transpose term moves.
     */
    const int delta = transpose - transpose_;

    int heldNote[128];
    int heldVeloc[128];
    int held = 0;

    /* The arrays are touched under drawMutex_ -- SetNote() writes them from
       the engine thread -- but the lock is never held across a signal
       emission. note_on and note_off reach thSynth, which takes its own
       mutex, while the engine thread can be inside SetNote() holding nothing
       and wanting drawMutex_: holding one and asking for the other from both
       sides is how a deadlock is spelled. So: snapshot under the lock, emit
       unlocked, write back under the lock. */
    {
        std::lock_guard<std::mutex> lock(drawMutex_);

        for (int i = 0; i < 128; i++)
        {
            if (!active_keys_[i])
                continue;

            heldNote[held] = i;
            heldVeloc[held] = active_veloc_[i] ? active_veloc_[i] : veloc3_;
            held++;

            active_keys_[i] = 0;
            active_veloc_[i] = 0;
        }
    }

    /* release at the number each was started with */
    for (int i = 0; i < held; i++)
        m_signal_note_off_(channel_, heldNote[i]);

    transpose_ = transpose;

    for (int i = 0; i < held; i++)
    {
        const int notenum = heldNote[i] + delta;

        /* a note shifted off either end of the range simply stops */
        if (notenum < 0 || notenum > 127)
            continue;

        m_signal_note_on_(channel_, notenum, heldVeloc[i]);

        std::lock_guard<std::mutex> lock(drawMutex_);

        active_keys_[notenum] = 1;
        active_veloc_[notenum] = heldVeloc[i];
    }

    /* keep the mouse's idea of what it is holding in step */
    if (mouse_notnum_ >= 0)
    {
        const int notenum = mouse_notnum_ + delta;

        mouse_notnum_ = (notenum >= 0 && notenum <= 127) ? notenum : -1;
    }

    /* transpose has been changed internally; emit the changed signal so
       widgets which interface with us will be able to update their transpose
       display, if any */
    m_signal_transpose_changed_(transpose_);

    queue_draw();
}

/* Called from the synthesizer engine thread, not the GUI one -- which is why
 * it ends in a dispatcher rather than a direct redraw.
 *
 * KeyboardWindow wraps its two callers in kbMutex_, but that is a different
 * mutex from the one drawKeyboard() holds, so it only serialised the two
 * callbacks against each other and did nothing at all about drawing. The one
 * that matters is drawMutex_, taken here. */
void Keyboard::SetNote (int note, bool state)
{
    if (note < 0 || note > 127)
        return;

    {
        std::lock_guard<std::mutex> lock(drawMutex_);

        active_keys_[note] = state ? 1 : 0;
        active_veloc_[note] = state ? veloc3_ : 0;
    }

    dispatchRedraw_();
}

int Keyboard::GetChannel (void)
{
    return channel_;
}

int Keyboard::GetTranspose (void)
{
    return transpose_;
}

bool Keyboard::GetNote (int note)
{
    if ((note < 0) || (note > 127))
       return false;

    return active_keys_[note] ? true : false;
}

/* signal accessor methods */
type_signal_note_on Keyboard::signal_note_on (void)
{
    return m_signal_note_on_;
}

type_signal_note_clear Keyboard::signal_note_clear (void)
{
    return m_signal_note_clear_;
}

type_signal_note_off Keyboard::signal_note_off (void)
{
    return m_signal_note_off_;
}

type_signal_channel_changed Keyboard::signal_channel_changed (void)
{
    return m_signal_channel_changed_;
}

type_signal_transpose_changed Keyboard::signal_transpose_changed (void)
{
    return m_signal_transpose_changed_;
}

/* overridden signal handlers */

/* on_realize no longer has anything to do. It used to reach through gobj() for
   the GdkWindow and build a GdkGC; GTK3 has neither, and the drawing context
   arrives with on_draw instead. */

void Keyboard::onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                       int height)
{
    /* The layout below is written in the fixed pixel units of key_sizes[], so
       rather than reworking all of it for a resizable widget, the natural
       drawing is scaled onto whatever size we were given. The widget still
       requests its natural size as a minimum, so this only ever scales up.
     *
     * scaleX_/scaleY_ are remembered because the mouse hit-testing has to undo
       exactly the same mapping -- otherwise clicks land on the wrong key as
       soon as the window is resized.
     *
     * The size arrives as arguments now rather than being read back out of
       the allocation, which is the same number by a shorter route. */
    scaleX_ = (img_width_  > 0) ? width  / (double)img_width_  : 1.0;
    scaleY_ = (img_height_ > 0) ? height / (double)img_height_ : 1.0;

    if (scaleX_ <= 0.0) scaleX_ = 1.0;
    if (scaleY_ <= 0.0) scaleY_ = 1.0;

    cr->save();
    cr->scale(scaleX_, scaleY_);

    drawKeyboard (cr);

    cr->restore();
}

void Keyboard::onFocusEnter (void)
{
    focus_box_ = true;

    queue_draw();
}

void Keyboard::onFocusLeave (void)
{
    focus_box_ = false;

    queue_draw();
}

void Keyboard::onPressed (int nPress, double x, double y)
{
    int veloc;

    (void)nPress;

    /* we want to steal focus on mouse-click */
    grab_focus();

    if (mouse_notnum_ >= 0) {    /* already active */
        m_signal_note_off_(channel_, mouse_notnum_);
        active_keys_[mouse_notnum_] = 0;
    }

    /* get note number */
    mouse_notnum_ = noteAt(x, y);

    if (mouse_notnum_ < 0) return;

    switch (click_->get_current_button())
    {
        case 1:    veloc = veloc3_; break;
        case 2:    veloc = veloc2_; break;
        case 3:    veloc = veloc1_; break;
        default:
            veloc = veloc0_;
            break;
    }

    m_signal_note_on_(channel_, mouse_notnum_, veloc);
    active_keys_[mouse_notnum_] = 1;

    /* SetTranspose re-triggers whatever is held, and reads the velocity from
       here. Without this a mouse-held note came back at veloc3_ -- the
       left-button velocity -- whichever button was actually down. */
    active_veloc_[mouse_notnum_] = veloc;

    queue_draw();

    mouse_veloc_ = veloc;    /* save velocity */
}

void Keyboard::onReleased (int nPress, double x, double y)
{
    (void)nPress; (void)x; (void)y;

    /* turn off if active */
    if (mouse_notnum_ >= 0) {
        m_signal_note_off_(channel_, mouse_notnum_);
        active_keys_[mouse_notnum_] = 0;
        active_veloc_[mouse_notnum_] = 0;
        queue_draw();
    }

    mouse_notnum_ = -1;
}

/* True to say the key has been dealt with and should go no further.
 *
 * This is where the vfunc's return value went: a controller claims a key by
 * saying so, rather than by a widget deep in the hierarchy returning true.
 * Anything that is not a note key is left alone, so the window's own
 * accelerators and the spin buttons still see it. */
bool Keyboard::onKeyPressed (guint keyval, guint keycode,
                             Gdk::ModifierType state)
{
    (void)keycode; (void)state;

    int notenum = keyval_to_notnum ((int)keyval);

    if (notenum < 0)
        return false;

    /* Auto-repeat arrives as a run of presses with no release between them,
       exactly as it did before. A note already sounding stays sounding. */
    if (active_keys_[notenum])
        return true;

    m_signal_note_on_(channel_, notenum, veloc3_);

    active_keys_[notenum] = 1;
    active_veloc_[notenum] = veloc3_;

    queue_draw();

    return true;
}

void Keyboard::onKeyReleased (guint keyval, guint keycode,
                              Gdk::ModifierType state)
{
    (void)keycode; (void)state;

    int notenum = keyval_to_notnum ((int)keyval);

    if (notenum >= 0) {    /* note event */
        m_signal_note_off_(channel_, notenum);
        active_keys_[notenum] = 0;
        queue_draw();
    }
}

void Keyboard::onMotion (double x, double y)
{
    if (mouse_notnum_ == -1)
        return;

    int notenum = noteAt(x, y);

    /* play only valid notes and only play a note once while the mouse is being
       moved over it */
    if ((notenum >= 0) && (notenum != mouse_notnum_))
    {
        active_keys_[mouse_notnum_] = 0;
        m_signal_note_off_(channel_, mouse_notnum_);

        active_keys_[notenum] = 1;
        active_veloc_[notenum] = mouse_veloc_;
        m_signal_note_on_(channel_, notenum, mouse_veloc_);

        mouse_notnum_ = notenum;

        queue_draw();
    }
}


void Keyboard::drawKeyboardFocus (const Cairo::RefPtr<Cairo::Context> &cr)
{
    /* Drawn here rather than asked of the theme. Gtk::Style and its paint_*
       methods went in GTK3, and Gtk::StyleContext::render_focus went in GTK4
       -- focus decoration is a CSS matter now, and a dashed rectangle drawn
       by hand is a smaller thing than a stylesheet for one widget.
     *
     * The colour does come from the theme, though. It was a fixed 0.2 grey,
     * which is all but invisible against a dark background -- and with the
     * Appearance menu the background can now be dark on any platform. The
     * style context's foreground colour is light or dark as the theme is, so
     * the rectangle stays visible either way. */
    const Gdk::RGBA fg = get_style_context()->get_color();

    cr->save();
    cr->set_source_rgb(fg.get_red(), fg.get_green(), fg.get_blue());
    cr->set_line_width(1.0);

    {
        std::vector<double> dashes;

        dashes.push_back(1.0);
        dashes.push_back(1.0);
        cr->set_dash(dashes, 0.0);
    }

    cr->rectangle(0.5, 0.5, img_width_ - 1.0, img_height_ - 1.0);
    cr->stroke();
    cr->restore();
}

/* Sets a packed 0x00RRGGBB colour on the context. */
void Keyboard::setColour (const Cairo::RefPtr<Cairo::Context> &cr,
                          unsigned int rgb)
{
    cr->set_source_rgb(((rgb >> 16) & 0xff) / 255.0,
                       ((rgb >>  8) & 0xff) / 255.0,
                       ( rgb        & 0xff) / 255.0);
}

/* GTK3 draws through a Cairo context supplied by on_draw(), and expects the
 * whole exposed region to be painted every time. The old code went straight at
 * the GdkWindow with a GdkGC whenever a key changed, and used `mode' to say
 * whether to repaint the borders and whether to repaint every key or only the
 * ones whose state had moved. Neither of those choices is ours to make now, so
 * the mode flag and the per-key "what did it look like last time" bookkeeping
 * are both gone: this paints the lot, and the callers just queue a redraw.
 *
 * That sounds wasteful and is not -- Cairo clips to the damaged region, so
 * repainting a key still only touches that key's pixels.
 */
void Keyboard::drawKeyboard (const Cairo::RefPtr<Cairo::Context> &cr)
{
    int i, j, k, l, s0, s1, s2, s3, s4, s5, s6;
    unsigned int c;

    drawMutex_.lock();

    s0 = key_sizes[cur_size_][0];    /* black key height        */
    s1 = key_sizes[cur_size_][1];    /* total height - b. key height    */
    s2 = key_sizes[cur_size_][2];    /* white key total width    */
    s3 = key_sizes[cur_size_][3];    /* black key width        */
    s4 = key_sizes[cur_size_][4];    /* white key width 1 (C, F)    */
    s5 = key_sizes[cur_size_][5];    /* white key width 2 (D, G, A)    */
    s6 = key_sizes[cur_size_][6];    /* white key width 3 (E, B)    */

    /* key borders */
    setColour(cr, color0);

    /* Undo the horizontal scale for stroke width so borders stay about one
       device pixel wide however far the keyboard is stretched. */
    cr->set_line_width((scaleX_ > 0.0) ? 1.0 / scaleX_ : 1.0);

    /* Cairo strokes centred on the path, so a 1px line wants a half-pixel
       offset to land on the pixel rather than straddling two. */
    cr->move_to(0, img_height_ - 0.5);
    cr->line_to(img_width_ - 1, img_height_ - 0.5);
    cr->stroke();

    /* One divider per white key, not one per note. This ran 128 times against
       a keyboard only KB_WHITE_KEYS wide, so it always drew 53 verticals past
       the right-hand edge -- invisible only while the widget's allocation
       happened to match its requested width, and plainly visible in a wider
       window. */
    i = KB_WHITE_KEYS;
    l = -1;
    do
    {
        l += s2;
        cr->move_to(l + 0.5, 0);
        cr->line_to(l + 0.5, img_height_ - 2);
    } while (--i);
    cr->stroke();

    j = s4;                /* black key x pos */
    l = 0;                /* white key x pos */
    i = k = 0;            /* key number (i: 0-127, k: 0-11) */
    /* draw keys */
    do
    {
        {
            if (((k >= 5) && (k & 1)) || ((k < 5) && !(k & 1)))
            {
                /* white keys */
                if (i == 60) 
                {
                    /* middle C */
                    c = (unsigned int)
                        (active_keys_[i] ? color6 : color3);
                }
                else
                {
                    c = (unsigned int)
                        (active_keys_[i] ? color4 : color1);
                }

                /* set color */
                setColour(cr, c);
                if ((k == 0) || (k == 5)) {
                    /* C, F */
                    cr->rectangle(l, 0, s4, s0);
                    cr->fill();
                } 
                else if ((k == 4) || (k == 11) || (i == 127))
                {
                    /* E, B */
                    cr->rectangle(l + s2 - s6 - 1, 0, s6, s0);
                    cr->fill();
                }
                else
                {
                    /* D, G, A */
                    cr->rectangle(l + s2 - s6 - 1, 0, s5, s0);
                    cr->fill();
                }
                cr->rectangle(l, s0, s2 - 1, s1 - 1);
                cr->fill();
            }
            else
            {
                /* black keys */
                c = (unsigned int)
                    (active_keys_[i] ? color5 : color2);
                /* set color */
                setColour(cr, c);
                if (active_keys_[i])
                {
                    /* backdrop, then the lit face inset by a pixel */
                    setColour(cr, color2);
                    cr->rectangle(j, 0, s3, s0);
                    cr->fill();

                    setColour(cr, c);
                    cr->rectangle(j + 1, 1, s3 - 2, s0 - 2);
                    cr->fill();
                }
                else
                {
                    cr->rectangle(j, 0, s3, s0);
                    cr->fill();
                }

            }
        }
        /* new x coordinate */
        if (((k >= 5) && (k & 1)) || ((k < 5) && !(k & 1)))
        {
            /* white keys */
            l += s2;
            if ((k == 4) || (k == 11))
            {
                /* skip E# and B# */
                j += s2;
            }
        }
        else
        {
            /* black keys */
            j += s2;
        }

        k = (k == 11 ? 0 : k + 1);

    } while (++i < 128);

    if (focus_box_)
    {
        drawKeyboardFocus (cr);
    }

    drawMutex_.unlock();

}

/* convert key value to note number        */
/* return value is -1 if key value is not valid    */
int    Keyboard::keyval_to_notnum (int key)
{
    char    *c;
    int    m, n, o;
    
    if ((key <= 0) || (key >= 256)) return -1;

    /* upper case -> lower case */
    if ((key >= 'A') && (key <= 'Z'))
    {
        key -= 'A'; key += 'a';
    }

    /* convert SHIFT characters */
    c = strchr (key_conv_in, key);
    if (c != NULL)
        key = key_conv_out[c - key_conv_in];

    /* find character in tables */
    c = strchr (keylist_1, key);
    if (c == NULL)
    {
        c = strchr (keylist_2, key);
        if (c == NULL) 
            return -1;    /* not found */

        n = (int) (c - keylist_2);
    }
    else
    {
        n = 14 + (int) (c - keylist_1);
    }

    n += 13;
    m = n % 14;
    o = n / 14;    /* octave */

    /* check for invalid keys (E# and B#) */
    if ((m == 5) || (m == 13))
        return -1;

    /* correct for missing black keys and transpose */
    if (m > 4)
        m--;

    n = 48 + transpose_ + 12 * o + m;

    if ((n < 0) || (n > 127))
        n = -1;

    return n;
}

/* Which note is under a point, or -1 when the point is off the keyboard. */
int Keyboard::noteAt (double px, double py) const
{
    int x, y, m, n, o;

    /* Undo the draw scale so the arithmetic below still works in the fixed
       key_sizes[] units it was written for. */
    x = (int)(px / (scaleX_ > 0.0 ? scaleX_ : 1.0));
    y = (int)(py / (scaleY_ > 0.0 ? scaleY_ : 1.0));

    /* check for valid coordinates */
    if ((x < 0) || (x >= img_width_) || (y < 0) || (y >= img_height_))
        return -1;

    /* calculate key number */
    n = (x / key_sizes[cur_size_][2]) << 1;
    m = n % 14;
    o = 12 * (n / 14) + m;

    if (y < key_sizes[cur_size_][0])
    {
        /* black keys */
        y = x - ((n >> 1) * key_sizes[cur_size_][2]);

        switch (m)
        {
            case 0:            /* C */
            case 6:            /* F */
                if (y >= key_sizes[cur_size_][4])
                    o++;
                break;
            case 4:            /* E */
            case 12:        /* B */
                if (y < (key_sizes[cur_size_][2]
                         - key_sizes[cur_size_][6] - 1))
                    o--;
                break;
            default:        /* D, G, A */
                if (y >= key_sizes[cur_size_][4])
                {
                    o++;
                } 
                else if (y < (key_sizes[cur_size_][2]
                              - key_sizes[cur_size_][6] - 1))
                {
                    o--;
                }
                break;
        }
    }

    /* correct for missing E# */
    if (m > 4)
        o--;
    if (o > 127)
        o = 127;

    return (int) o;
}

