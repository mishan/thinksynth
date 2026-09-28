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

#ifndef KEYBOARD_PANEL_H
#define KEYBOARD_PANEL_H

#include <mutex>

/* The on-screen keyboard and its channel and transpose controls, as a
   strip along the bottom of the main window (MainSynthWindow). It was a
   window of its own, which put the keys a window away from the channel
   they played and let them sit behind it. */
class KeyboardPanel : public Gtk::Box
{
public:
    KeyboardPanel (thSynth *synth);
    ~KeyboardPanel (void);

    /* Aims the keys at `chan' (0-based), as picking it in the spinner
       does. The main window calls this when a channel's tab is chosen. */
    void setChannel (int chan);

protected:
    void eventNoteOn (int chan, int note, float veloc);
    void eventNoteOff (int chan, int note);
    void eventChannelChanged (int chan);
    void eventTransposeChanged (int trans);

    void synthEventNoteOn (int chan, float note, float veloc);
    void synthEventNoteOff (int chan, float note);

    void changeChannel (void);
    void changeTranspose (void);
    void keyboardReset (void);
    void keyboardResetKeys (void);

    /* Scrolling over the controls changes channel. A controller now, and one that
       has to be asked for the kinds of scroll it wants -- there is no event
       mask to widen. */
    bool onScroll (double dx, double dy);

    Glib::RefPtr<Gtk::EventControllerScroll> scroll_;

    thSynth *synth_;
private:
    std::mutex kbMutex_;

    Keyboard *keyboard_;

    /* widgets */
    Gtk::Grid *ctrlTable_;

    Gtk::Label *chanLbl_;
    Gtk::SpinButton *chanBtn_;
    Glib::RefPtr<Gtk::Adjustment> chanVal_;

    Gtk::Label *transLbl_;
    Gtk::SpinButton *transBtn_;
    Glib::RefPtr<Gtk::Adjustment> transVal_;

    Gtk::Button *resetBtn_;
};

#endif /* KEYBOARD_PANEL_H */
