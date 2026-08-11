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
class SaveButton;

using namespace std;

class MainSynthWindow : public Gtk::Window
{
public:
    MainSynthWindow (gthAudio *);
    ~MainSynthWindow (void);

    /* Takes what the preferences say about the window itself.
     *
     * Separate from the constructor because the file has not been read when
     * that runs, and cannot have been: loading it puts patches on channels,
     * and that wants a window already there to notice. So main builds the
     * window, reads the preferences, and then hands over the part of them
     * that is about the window. Call before showing it -- the size is a
     * default size, which a window that is already on screen ignores. */
    void applyPrefs (void);

protected:
    void populateMenu (void);
    void menuKeyboard (void);
    void menuPatchSel (void);
    void menuMidiMap (void);
    void menuQuit (void);
    void menuAbout (void);

    /* `tip' is the full path, shown on hover. The tab itself carries only the
       basename -- see the comment where it is built. */
    void append_tab (const string &tabName, const string &tip, int num,
                     bool is_real);

    /* A tab label that behaves in a vertical strip: left-aligned, and
       ellipsised rather than widening the strip to fit the longest name. */
    Gtk::Widget *makeTabLabel (const string &text, const string &tip);

    /* The strip across the top of a patch page: which patch it is, its
       amplitude, and what can be done with it. Above the Overview/Nodes
       notebook rather than inside either, because it is about the patch and
       not about one view of it. */
    Gtk::Widget *makePatchBar (int chan);

    void onAmpSlider (Gtk::Scale *scale, int chan);
    void onPatchDirty (int chan, SaveButton *button, int mine);
    void onAmpArgChanged (thArg *arg, int chan);

    void onSavePatch (int chan);
    void onSavePatchAs (int chan);

    /* The other half of each of those: a GTK4 chooser is answered after the
       function that opened it has returned, so everything that used to follow
       run() lives here. Each owns the dialog it is handed. */
    void onSavePatchAsResponse (int response, Gtk::FileChooserDialog *fileSel,
                                int chan);
    void onBrowseResponse (int response, Gtk::FileChooserDialog *fileSel,
                           int pagenum);

    /* The write itself, run once the click that asked for it has returned.
       Saving emits signal_patches_changed, which tears down and rebuilds every
       page -- including the button being clicked. */
    void doSavePatch (string file, int chan);

    /* Between confirmOverwrite and doSavePatch: something to hand the
       confirmation that takes no arguments. */
    void queueSavePatch (string file, int chan);

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

    /* Empties the notebook. Not inline at the three call sites any more,
       because removing pages has a consequence worth naming once -- see the
       definition. */
    void clearPages (void);

    /* The size the window had while it was on screen, and the two ends of
       remembering it. */
    void captureSize (void);

    /* Takes the size, hides the window and stops the application. Both ways
       out of the program go through it; see the definition for why neither
       lets GTK close the window itself. */
    void shutdown (void);
    bool onCloseRequest (void);
    void rememberGeometry (void);

    void onPatchesChanged (void);

    /* Hides a secondary window instead of letting it be destroyed, so it can
       be presented again. Returns true: the close is handled. */
    bool onSubWindowClose (Gtk::Window *window);
    void onSwitchPage (Gtk::Widget *page, guint pagenum);
    void onMasterGain (void);

    /* Bolds the current tab's label and unbolds the rest. */
    void highlightTab (int pagenum);
    void onDspEntryActivate (void);
    void onBrowseButton (void);
    void onPatchLoadError (const char* failure);

    Gtk::Box vbox_{Gtk::Orientation::VERTICAL};

    /* Names an action, gives it something to do, and optionally a key.
     *
     * GTK4 menus are a model plus a set of actions: the item carries a name
     * like "win.keyboard" and the behaviour hangs off the window under that
     * name, rather than a callback hanging off the item. Accelerators bind to
     * the action too, so they work whether or not the menu was ever opened.
     */
    void addAction (const Glib::ustring &name,
                    const sigc::slot<void ()> &handler,
                    const char *accel = NULL);

    /* Binds the accelerators once there is an application to bind them to.
       They belong to it, not to the window, and the window is built before it
       has one. */
    void onApplicationSet (void);

    Glib::RefPtr<Gio::SimpleActionGroup> actions_;
    Gtk::PopoverMenuBar *menuBar_;

    std::vector<std::pair<Glib::ustring, Glib::ustring> > accels_;

    Gtk::Entry dspEntry_;
    Gtk::Label dspEntryLbl_;
    Gtk::Button dspBrowseBtn_;
    Gtk::Box dspEntryBox_{Gtk::Orientation::HORIZONTAL};

    Gtk::Notebook notebook_;

    /* Master output level, for the whole synth rather than one channel. The
       engine has had setMasterGain since the gain-staging work; this is the
       first thing to offer it. */
    Gtk::Label masterLbl_;
    Gtk::Scale masterScale_{Gtk::Orientation::HORIZONTAL};

    PatchSelWindow *patchSel_;
    KeyboardWindow *kbWin_;
    AboutBox *aboutBox_;
    MidiMap *midiMap_;
    /* The node editor on each page, built the first time its tab is looked
       at. Building one scans the whole plugin directory for the palette, so
       sixteen of them up front would be sixteen scans for the one you
       wanted. */
    std::map<Gtk::Widget *, NodeEditor *> editors_;

    /* The amplitude slider on each patch page, and its subscription to the
       arg behind it.
     *
     * The channel's `amp' outlives the page -- it belongs to the thMidiChan,
     * and every patch load rebuilds all sixteen pages -- so the slot that
     * follows it back to the slider goes through the channel number and looks
     * the current slider up, rather than capturing one. The connections are
     * dropped in populate() so a session's worth of reloads does not leave a
     * subscription behind for every page that ever existed. */
    std::map<int, Gtk::Scale *> ampScales_;
    std::vector<sigc::connection> ampConns_;
    std::vector<sigc::connection> saveConns_;

    /* True while the notebook's pages are being taken away, and for good
       once the window is being destroyed.
     *
     * Removing a page destroys the Overview/Nodes notebook on it, and a
     * notebook losing pages switches to whichever is left -- which arrives
     * here as a request to build a node editor into a page that is on its way
     * out. */
    bool tearingDown_;
private:
    gthAudio *audio_;
    string prevDir_;

    /* Last size seen on screen, for the preferences. Tracked as it changes
       rather than read at the end: by the time the window is being destroyed
       it has been hidden, and a hidden window's size is not a question with a
       reliable answer. */
    int width_, height_;

};

#endif /* MAIN_SYNTH_WINDOW_H */
