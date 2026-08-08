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
    /* allow widget to receive mouse/keypress events */
    add_events(Gdk::ALL_EVENTS_MASK);

    /* allow the widget to grab focus and process keypress events */
    set_can_focus(true);

    focus_box_ = false;

    /* clear previous key state */
    for (int i = 0; i < 128; i++)
    {
        prv_active_keys_[i] = -2;
        active_keys_[i] = 0;
    }

    dispatchRedraw_.connect(
        sigc::mem_fun(*this, &Keyboard::queueRedraw));
}

void Keyboard::resetKeys (void)
{
    /* clear previous key state */
    for (int i = 0; i < 128; i++)
    {
        prv_active_keys_[i] = -2;
        
        if (active_keys_[i] == 1)
            m_signal_note_off_(channel_, i);
        
        active_keys_[i] = 0;
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
        prv_active_keys_[i] = -1;
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

    /* keyval_to_notnum() adds transpose_, so a key held across a transpose
       change would release the wrong note and leave the original hanging.
       SetChannel has always done this; SetTranspose never did. */
    releaseAllNotes();

    transpose_ = transpose;

    /* transpose has been changed internally; emit the changed signal so
       widgets which interface with us will be able to update their transpose
       display, if any */
    m_signal_transpose_changed_(transpose_);

    queue_draw();
}

void Keyboard::SetNote (int note, bool state)
{
    active_keys_[note] = state ? 1 : 0;
    prv_active_keys_[note] = -1;

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

bool Keyboard::on_draw (const Cairo::RefPtr<Cairo::Context> &cr)
{
    /* The layout below is written in the fixed pixel units of key_sizes[], so
       rather than reworking all of it for a resizable widget, the natural
       drawing is scaled onto whatever allocation we were given. The widget
       still requests its natural size as a minimum, so this only ever scales
       up.
     *
     * scaleX_/scaleY_ are remembered because the mouse hit-testing has to undo
       exactly the same mapping -- otherwise clicks land on the wrong key as
       soon as the window is resized. */
    Gtk::Allocation alloc = get_allocation();

    scaleX_ = (img_width_  > 0) ? alloc.get_width()  / (double)img_width_  : 1.0;
    scaleY_ = (img_height_ > 0) ? alloc.get_height() / (double)img_height_ : 1.0;

    if (scaleX_ <= 0.0) scaleX_ = 1.0;
    if (scaleY_ <= 0.0) scaleY_ = 1.0;

    cr->save();
    cr->scale(scaleX_, scaleY_);

    drawKeyboard (cr);

    cr->restore();

    return true;
}

bool Keyboard::on_focus_in_event (GdkEventFocus *f)
{
    focus_box_ = true;

    queue_draw();

    return true;
}

bool Keyboard::on_focus_out_event (GdkEventFocus *f)
{
    focus_box_ = false;

    queue_draw();

    return true;
}

bool Keyboard::on_button_press_event (GdkEventButton *b)
{
    int    veloc;

    /* we want to steal focus on mouse-click */
//    grab_focus ();

    if (mouse_notnum_ >= 0) {    /* already active */
        m_signal_note_off_(channel_, mouse_notnum_);
        active_keys_[mouse_notnum_] = 0;
    }
        
    /* get note number */
    mouse_notnum_ = get_coord ();
        
    if (mouse_notnum_ < 0) return false;
        
    switch (b->button) 
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

    queue_draw();

    mouse_veloc_ = veloc;    /* save velocity */

    return true;
}

bool Keyboard::on_button_release_event (GdkEventButton *b)
{
    /* turn off if active */
    if (mouse_notnum_ >= 0) {
        m_signal_note_off_(channel_, mouse_notnum_);
        active_keys_[mouse_notnum_] = 0;
        queue_draw();
    }

    mouse_notnum_ = -1;

    return true;
}

bool Keyboard::on_key_press_event (GdkEventKey *k)
{
    /* other keys */
    int notenum = keyval_to_notnum (k->keyval);

    if (notenum >= 0) {    /* note event */
        if (active_keys_[notenum])
        {
            return true;
        }

        m_signal_note_on_(channel_, notenum, veloc3_);
        
        active_keys_[notenum] = 1;
        
        queue_draw();
    }

    return true;
}

bool Keyboard::on_key_release_event (GdkEventKey *k)
{
    /* other keys */
    int notenum = keyval_to_notnum (k->keyval);
    if (notenum >= 0) {    /* note event */
        m_signal_note_off_(channel_, notenum);
        active_keys_[notenum] = 0;
        queue_draw();
    }

    return true;
}

bool Keyboard::on_motion_notify_event (GdkEventMotion *e)
{
    if (mouse_notnum_ == -1)
        return true;

    int notenum = get_coord();

    /* play only valid notes and only play a note once while the mouse is being
       moved over it */
     if ((notenum >= 0) && (notenum != mouse_notnum_))
    {
         active_keys_[mouse_notnum_] = 0;
        m_signal_note_off_(channel_, mouse_notnum_);

        active_keys_[notenum] = 1;
        m_signal_note_on_(channel_, notenum, mouse_veloc_);

        mouse_notnum_ = notenum;

        queue_draw();
    }

    return true;
}

void Keyboard::drawKeyboardFocus (const Cairo::RefPtr<Cairo::Context> &cr)
{
    /* Gtk::Style and its paint_* methods went with GTK3's move to CSS; the
       theme is asked to render through the style context now. */
    get_style_context()->render_focus(cr, 0, 0, img_width_, img_height_);
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
 * the mode flag and prv_active_keys_ bookkeeping are gone: this paints the lot,
 * and the callers just queue a redraw.
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

/* get mouse pointer coordinates, and convert to key number */
/* return -1 if pointer is not in keyboard area */
int    Keyboard::get_coord (void)
{
    int         x, y, m, n, o;
    Gdk::ModifierType mask;

    /* gdk_window_get_pointer is deprecated in GTK3 in favour of asking the
       seat's pointer device where it is. */
    Glib::RefPtr<Gdk::Window> win = get_window();

    if (!win)
        return -1;

    win->get_device_position(
        Gdk::Display::get_default()->get_default_seat()->get_pointer(),
        x, y, mask);

    /* Undo the on_draw scale so the arithmetic below still works in the fixed
       key_sizes[] units it was written for. */
    if (scaleX_ > 0.0)
        x = (int)(x / scaleX_);
    if (scaleY_ > 0.0)
        y = (int)(y / scaleY_);

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

