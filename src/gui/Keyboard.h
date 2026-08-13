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

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <mutex>

/* this widget's custom signals */
typedef sigc::signal<void()>                  type_signal_note_clear;
typedef sigc::signal<void(int, int, float)> type_signal_note_on;
typedef sigc::signal<void(int, int)>        type_signal_note_off;
typedef sigc::signal<void(int)>             type_signal_channel_changed;
typedef sigc::signal<void(int)>             type_signal_transpose_changed;

class Keyboard : public Gtk::DrawingArea
{
public:
    Keyboard (void);
    ~Keyboard (void);

    /* parameter mutator methods */
    void SetChannel   (int argchan);
    void SetTranspose (int argtranspose);
    void SetNote      (int note, bool state);
    
    void resetKeys (void);
    
    /* parameter accessor methods */
     int  GetChannel   (void);
    int  GetTranspose (void);
    bool GetNote      (int note);
    
    /* signal accessor methods */
    type_signal_note_clear        signal_note_clear        (void);
    type_signal_note_on           signal_note_on           (void);
     type_signal_note_off          signal_note_off          (void);
    type_signal_channel_changed   signal_channel_changed   (void);
    type_signal_transpose_changed signal_transpose_changed (void);
protected:
    /* Sends note-off for everything currently held. Anything that changes the
       key-to-note mapping must call this before changing it. */
    void releaseAllNotes (void);

    void drawKeyboard (const Cairo::RefPtr<Cairo::Context> &cr);
     void drawKeyboardFocus (const Cairo::RefPtr<Cairo::Context> &cr);
    static void setColour (const Cairo::RefPtr<Cairo::Context> &cr,
                           unsigned int rgb);

    /* Dispatcher target: redraws happen through the widget now. */
    void queueRedraw (void) { queue_draw(); }
    
    /* Input, as GTK4 delivers it.
     *
     * The on_*_event vfuncs are gone -- every one of them -- and a widget no
     * longer decides what happens by returning true or false from an
     * override. Input arrives at controllers attached to the widget, each
     * handling one kind of thing, and the coordinates come with the event
     * rather than being fetched from the pointer afterwards. That last part
     * is the improvement: see noteAt(). */
    void onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                 int height);
    void onPressed (int nPress, double x, double y);
    void onReleased (int nPress, double x, double y);
    bool onKeyPressed (guint keyval, guint keycode, Gdk::ModifierType state);
    void onKeyReleased (guint keyval, guint keycode, Gdk::ModifierType state);
    void onMotion (double x, double y);
    void onFocusEnter (void);
    void onFocusLeave (void);

    int channel_;
    int transpose_;
private:
    /* Which note is under (x, y), or -1.
     *
     * This used to be get_coord(), which asked the pointer where it was
     * rather than being told -- a GTK3 habit that GTK4 makes unnecessary and
     * awkward, since Gdk::Window is gone along with get_device_position. The
     * coordinates are the ones the controller was handed, so they are the
     * coordinates of the event being handled rather than of wherever the
     * mouse has got to by now. */
    int noteAt (double x, double y) const;
    int keyval_to_notnum (int key);

    /* Held because a controller lives only as long as its RefPtr does. */
    Glib::RefPtr<Gtk::GestureClick> click_;
    Glib::RefPtr<Gtk::EventControllerKey> keys_;
    Glib::RefPtr<Gtk::EventControllerMotion> motion_;
    Glib::RefPtr<Gtk::EventControllerFocus> focus_;

    type_signal_note_clear        m_signal_note_clear_;
    type_signal_note_on           m_signal_note_on_;
    type_signal_note_off          m_signal_note_off_;
    type_signal_channel_changed   m_signal_channel_changed_;
    type_signal_transpose_changed m_signal_transpose_changed_;

    /* lower-level widget stuff */
    std::mutex drawMutex_;
    Glib::Dispatcher dispatchRedraw_;

    bool focus_box_;

    /* Ratio of the widget's allocation to its natural key_sizes[] geometry.
       on_draw scales by these; the mouse hit-testing divides by them. */
    double scaleX_, scaleY_;

    /* keyboard stuff */
    int img_width_, img_height_;
    int active_keys_[128];

    /* Velocity each held note was started with, so a transpose can re-trigger
       it at the same strength. Indexed like active_keys_. */
    int active_veloc_[128];

    int mouse_notnum_;
    int veloc0_;
    int veloc1_;
    int veloc2_;
    int veloc3_;
    int mouse_veloc_;

    int cur_size_;
};

#endif /* KEYBOARD_H */
