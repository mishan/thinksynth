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

#ifndef KEYBOARD_H
#define KEYBOARD_H

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
    
    /* overridden signal handlers */
    virtual bool on_draw                 (const Cairo::RefPtr<Cairo::Context> &cr);
     virtual bool on_focus_in_event       (GdkEventFocus  *f);
    virtual bool on_focus_out_event      (GdkEventFocus  *f);
    virtual bool on_button_press_event   (GdkEventButton *b);
    virtual bool on_button_release_event (GdkEventButton *b);
    virtual bool on_key_press_event      (GdkEventKey    *k);
    virtual bool on_key_release_event    (GdkEventKey    *k);
    virtual bool on_motion_notify_event  (GdkEventMotion *e);

    int channel_;
    int transpose_;
private:
    int get_coord ();
    int keyval_to_notnum (int key);

    type_signal_note_clear        m_signal_note_clear_;
    type_signal_note_on           m_signal_note_on_;
    type_signal_note_off          m_signal_note_off_;
    type_signal_channel_changed   m_signal_channel_changed_;
    type_signal_transpose_changed m_signal_transpose_changed_;

    /* lower-level widget stuff */
    Glib::Mutex drawMutex_;
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
