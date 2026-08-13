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

#ifndef MIDI_MAP_H
#define MIDI_MAP_H

/* One row of the connection list.
 *
 * A Gtk::ColumnView's model holds objects rather than a record of typed
 * columns, so a row is a small GObject and the columns are functions of it.
 * The engine's own numbering is kept -- channel and controller as the
 * connection reports them -- and the +1 for display happens where it is
 * displayed, which is the opposite way round from the ListStore this replaces.
 * onConnectionMoved wants the real numbers back, and it used to get them by
 * subtracting one from what had been written for the user's benefit.
 */
class MidiMapRow : public Glib::Object
{
public:
    static Glib::RefPtr<MidiMapRow> create (int chan, int controller,
                                            const Glib::ustring &instrument,
                                            const Glib::ustring &argName)
    {
        return Glib::make_refptr_for_instance(
            new MidiMapRow(chan, controller, instrument, argName));
    }

    int chan (void) const { return chan_; }
    int controller (void) const { return controller_; }
    Glib::ustring instrument (void) const { return instrument_; }
    Glib::ustring argName (void) const { return argName_; }

protected:
    MidiMapRow (int chan, int controller, const Glib::ustring &instrument,
                const Glib::ustring &argName)
        : Glib::ObjectBase(typeid(MidiMapRow)),
          chan_(chan), controller_(controller),
          instrument_(instrument), argName_(argName) { }

private:
    int chan_;
    int controller_;
    Glib::ustring instrument_;
    Glib::ustring argName_;
};


class MidiMap : public Gtk::Window
{
public:
    MidiMap (thSynth *);
    ~MidiMap (void);

    void set_sensitive (bool sensitive);

protected:
    void onAddButton (void);
    void onDelButton (void);
    void onChannelChanged (void);
    void onControllerChanged (void);
    /* These took a GdkEventButton they never looked at -- they were bound to
       a menu item's button-press in the days when the destination pickers
       were menus. There is no event to take now. */
    void onDestChanComboChanged (int chan);
    void onDestArgComboChanged (thArg *arg);
    void onMinChanged (void);
    void onMaxChanged (void);
    void onExpToggled (void);
    void onConnectionSelected (void);
    void onConnectionMoved (void);
    void onPatchChanged (void);

    Glib::RefPtr<Gtk::Adjustment> channelAdj_;
    Glib::RefPtr<Gtk::Adjustment> controllerAdj_;
    Glib::RefPtr<Gtk::Adjustment> minAdj_;
    Glib::RefPtr<Gtk::Adjustment> maxAdj_;

    Gtk::Box *mainVBox_;
    Gtk::Box *inputVBox_;
    Gtk::Box *srcDestHBox_;
    Gtk::Box *newConnectionHBox_;
    Gtk::Frame *newConnectionFrame_;
    Gtk::Box *destinationHBox_;
    Gtk::Frame *destinationFrame_;
    Gtk::Box *detailsHBox_;
    Gtk::Frame *detailsFrame_;
    Gtk::Box *buttonsHBox_;
    Gtk::Label *channelLbl_;
    Gtk::SpinButton *channelSpinBtn_;
    Gtk::Label *controllerLbl_;
    Gtk::SpinButton *controllerSpinBtn_;
    Gtk::ComboBoxText *destChanCombo_;
    Gtk::ComboBoxText *destArgCombo_;

    /* Repopulating a ComboBoxText emits signal_changed; without this the
       selection handlers would recurse back into the fill functions. */
    bool rebuilding_;
    Gtk::Label *minLbl_;
    Gtk::SpinButton *minSpinBtn_;
    Gtk::Label *maxLbl_;
    Gtk::SpinButton *maxSpinBtn_;
    Gtk::Label *expLbl_;
    Gtk::CheckButton *expCheckBtn_;
    Gtk::Button *addBtn_;
    Gtk::Button *delBtn_;

    Gtk::Frame *connectFrame_;
    Gtk::ScrolledWindow connectScroll_;
    Gtk::ColumnView connectView_;
    Glib::RefPtr<Gio::ListStore<MidiMapRow> > connectModel_;
    Glib::RefPtr<Gtk::SingleSelection> connectSelection_;

private:
    void onDestChanSelected (void);
    void onDestArgSelected (void);
    void fillDestChanCombo (void);
    void setDestChanCombo (void);
    void fillDestArgCombo (int chan);
    void setDestArgCombo (int chan);
    void populateConnections (void);

    thSynth *synth_;
    int selectedChan_;
    int selectedController_;
    int selectedDestChan_;
//    string selectedInstrument_;
    float selectedMin_;
    float selectedMax_;
    int selectedExp_;
    thArg *selectedArg_;
};

#endif /* MIDI_MAP_H */
