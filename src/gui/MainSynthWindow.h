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

#ifndef MAIN_SYNTH_WINDOW_H
#define MAIN_SYNTH_WINDOW_H

class gthAudio;
class gthPrefs;
class AboutBox;
class MidiMap;
class NodeEditor;

using namespace std;

class MainSynthWindow : public Gtk::Window
{
public:
    MainSynthWindow (gthAudio *);
    ~MainSynthWindow (void);

protected:
    void populateMenu (void);
    void menuKeyboard (void);
    void menuPatchSel (void);
    void menuMidiMap (void);
    void menuNodes (void);
    void menuQuit (void);
    void menuAbout (void);

    /* `tip' is the full path, shown on hover. The tab itself carries only the
       basename -- see the comment where it is built. */
    void append_tab (const string &tabName, const string &tip, int num,
                     bool is_real);

    /* A tab label that behaves in a vertical strip: left-aligned, and
       ellipsised rather than widening the strip to fit the longest name. */
    Gtk::Widget *makeTabLabel (const string &text, const string &tip);

    /* Which node each control drives, keyed by control name. Controls read
       by more than one node are left out: they belong to no single node. */
    std::map<string, string> inferGroups (int chan);

    /* A .dsp name as a patch stores it -- usually bare -- as a path that can
       actually be opened. */
    string resolveDspPath (const string &named);

    /* Builds the node editor for a page the first time its tab is shown. */
    void onSubTab (Gtk::Widget *page, guint num, Gtk::Widget *holder,
                   string dspFile, int chan);
    void populate (void);

    void onPatchesChanged (void);
    void onAboutBoxHide (void);
    void onPatchSelHide (void);
    void onMidiMapHide (void);
    void onKeyboardHide (void);
    void onSwitchPage (Gtk::Widget *page, guint pagenum);
    void onMasterGain (void);

    /* Bolds the current tab's label and unbolds the rest. */
    void highlightTab (int pagenum);
    void onDspEntryActivate (void);
    void onBrowseButton (void);
    void onPatchLoadError (const char* failure);

    Gtk::VBox vbox_;

    Gtk::MenuItem *addMenuItem (Gtk::Menu &menu, const Glib::ustring &label,
                                const sigc::slot<void> &handler,
                                const char *accel = NULL);

    Gtk::MenuBar menuBar_;
    Gtk::Menu menuFile_;
    Gtk::Menu menuHelp_;

    Gtk::Entry dspEntry_;
    Gtk::Label dspEntryLbl_;
    Gtk::Button dspBrowseBtn_;
    Gtk::HBox dspEntryBox_;

    Gtk::Notebook notebook_;

    /* Master output level, for the whole synth rather than one channel. The
       engine has had setMasterGain since the gain-staging work; this is the
       first thing to offer it. */
    Gtk::Label masterLbl_;
    Gtk::HScale masterScale_;

    PatchSelWindow *patchSel_;
    KeyboardWindow *kbWin_;
    AboutBox *aboutBox_;
    MidiMap *midiMap_;
    /* The Overview/Nodes notebook on each patch page, indexed by channel, so
       the menu item can switch the current page to its Nodes tab. NULL for a
       channel with no patch. */
    std::vector<Gtk::Notebook *> subTabs_;

    /* The node editor on each page, built the first time its tab is looked
       at. Building one scans the whole plugin directory for the palette, so
       sixteen of them up front would be sixteen scans for the one you
       wanted. */
    std::map<Gtk::Widget *, NodeEditor *> editors_;
private:
    gthAudio *audio_;
    string prevDir_;

};

#endif /* MAIN_SYNTH_WINDOW_H */
