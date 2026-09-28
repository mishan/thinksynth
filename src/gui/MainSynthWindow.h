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

#ifndef MAIN_SYNTH_WINDOW_H
#define MAIN_SYNTH_WINDOW_H

/* For gthThemeChoice, which the Appearance menu handlers below take. */
#include "../gthTheme.h"

class gthAudio;
class gthPrefs;
class AboutBox;
class MidiMap;
class NodeEditor;
class SaveButton;
class Composer;
class KeyboardPanel;
class PatchSelPanel;
class Panes;
typedef struct _MlnPanes MlnPanes;

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

    /* Appearance: the menu, the preference and the live system-theme
       notification all funnel through setTheme. */
    void setTheme (gthThemeChoice choice);
    void menuTheme (const Glib::ustring &target);
    void onSystemThemeChanged (void);

protected:
    void populateMenu (void);
    void menuQuit (void);
    void menuAbout (void);

    /* The panes, their places the first time, and the layout kept from
       the last run. */
    void buildPanes (void);
    Gtk::Widget &scrolled (Gtk::Widget &content, bool down);

    /* One channel's row in Channels, its page in Patch params and its
       holder in Patch graph. `tip' is the full path, shown on hover; the
       row carries only the basename -- see the comment where it is
       built. */
    void appendChannel (const string &name, const string &tip, int num,
                        bool is_real);

    /* A row in Channels: left-aligned, and ellipsized rather than widening
       the pane to fit the longest name. */
    Gtk::Widget *makeChannelLabel (const string &text, const string &tip);

    /* The channel the panes are about: its row, its parameters, its graph
       and the keys. */
    void selectChannel (int chan);
    void onChannelRow (Gtk::ListBoxRow *row);

    /* The strip across the top of a patch's parameters: which patch it is,
       its amplitude, and what can be done with it. Over the parameters
       rather than among them, because it is about the patch and not about
       one of its parameters. */
    Gtk::Widget *makePatchBar (int chan);

    /* The channel effect's block on a patch page: which graph is on the
     * channel's sum, the two buttons that change that, and a second
     * parameter panel over the effect's own chanargs.
     *
     * A block of its own rather than more rows in DSP Parameters, because an
     * effect's `@a' and an instrument's are two different numbers and a panel
     * that ran them together would be saying otherwise. */
    Gtk::Widget *makeEffectFrame (int chan);

    void onEffectBrowse (int chan);

    /* Both graph choosers: the same browser over the two halves of the
       corpus, since the only difference between them is which half. */
    void openDspBrowser (bool effects, int chan);
    void onEffectChosen (string picked, int chan);
    void onEffectRemove (int chan);

    /* Everything a change of effect has to do to the window: the page is
       rebuilt, because the second parameter panel is part of it. */
    void reloadPages (int chan);

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
    /* The browser's answer. Not a chooser response: DspBrowser hands back the
       name a file names itself by, and answers once. */
    void onBrowseChosen (string picked, int pagenum);

    /* The write itself, run once the click that asked for it has returned.
       Saving emits signal_patches_changed, which tears down and rebuilds every
       page -- including the button being clicked. */
    void doSavePatch (string file, int chan);

    /* Between confirmOverwrite and doSavePatch: something to hand the
       confirmation that takes no arguments. */
    void queueSavePatch (string file, int chan);

    /* A .dsp name as a patch stores it -- usually bare -- as a path that can
       actually be opened. */
    string resolveDspPath (const string &named);

    /* Builds the channel's node editor, the first time its graph is in
       view. */
    void ensureEditor (int chan);
    void populate (void);

    /* Empties Channels and the two pages per channel. */
    void clearPages (void);

    void onPaneShown (const string &id, bool visible);

    /* The composer's panes: which of them are in view decides whether it
       is started, drawn and editing. */
    void syncComposer (void);

    /* The View menu's ticks: a pane is ticked while the layout holds it,
       in front or behind a tab, and not while it is in the drawer. */
    void syncPaneActions (void);
    void togglePane (const string &id);

    /* The layout, kept in panes.ini beside thinkrc: written a moment after
       the last change rather than on every step of a divider's drag, and
       at the end. */
    static string layoutPath (void);
    string readLayout (void);
    void onLayoutKept (const string &mode, const string &text);
    void writeLayout (void);

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

    /* Hides a secondary window -- About -- instead of
       letting it be destroyed, so it can be presented again. Returns true:
       the close is handled. */
    bool onSubWindowClose (Gtk::Window *window);

    /* Gives a secondary window Ctrl+W. See the definition for why it is a
       controller rather than an accelerator like the rest of them. */
    void addCloseAccel (Gtk::Window *window);

    /* A window for panes moved out of this one: the same actions and
       keys as here. See the definition. */
    static GtkWindow *makePaneWindow (MlnPanes *panes, gpointer self);
    bool onSubWindowKey (guint keyval, guint keycode, Gdk::ModifierType state,
                         Gtk::Window *window);
    void onMasterGain (void);

    void onDspEntryActivate (void);
    void onBrowseButton (void);
    void onPatchLoadError (const char* failure);


    /* Names an action, gives it something to do, and optionally a key.
     *
     * GTK4 menus are a model plus a set of actions: the item carries a name
     * like "win.pane-keyboard" and the behaviour hangs off the window under
     * that name, rather than a callback hanging off the item. Accelerators bind to
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

    Glib::RefPtr<Gio::SimpleAction> themeAction_;

    /* View's panes, by id: stateful, so the menu shows which are up. */
    std::map<string, Glib::RefPtr<Gio::SimpleAction> > paneActs_;

    Gtk::HeaderBar header_;
    Gtk::MenuButton menuBtn_;

    std::vector<std::pair<Glib::ustring, Glib::ustring> > accels_;

    Gtk::Entry dspEntry_;
    Gtk::Label dspEntryLbl_;
    Gtk::Button dspBrowseBtn_;
    Gtk::Box dspEntryBox_{Gtk::Orientation::HORIZONTAL};

    /* Owned here and destroyed first, in the destructor: a pane going out
       of view says so, and nothing it says should reach a window half torn
       down. */
    Panes *panes_;

    /* Channels: a row for each of the sixteen. */
    Gtk::ScrolledWindow chanScroll_;
    Gtk::ListBox chanList_;

    /* Patch params: the DSP entry over a page for each channel. */
    Gtk::Box paramBox_{Gtk::Orientation::VERTICAL};
    Gtk::Stack paramStack_;

    /* Patch graph: a holder for each channel, filled with its editor the
       first time the graph is looked at. */
    Gtk::Stack nodeStack_;
    std::vector<Gtk::Box *> holders_;

    /* The channel the panes are about, and true while selectChannel is
       moving the list's selection to match. */
    int chan_;
    bool selecting_;

    /* Master output level, for the whole synth rather than one channel. The
       engine has had setMasterGain since the gain-staging work; this is the
       first thing to offer it. */
    Gtk::Label masterLbl_;
    Gtk::Scale masterScale_{Gtk::Orientation::HORIZONTAL};

    PatchSelPanel *patchSel_;
    KeyboardPanel *kbPanel_;
    AboutBox *aboutBox_;
    MidiMap *midiMap_;
    /* The piece: its canvas, roll, settings and selection are panes, and
       its transport is in the title bar. */
    Composer *composer_;
    Gtk::Box titleBox_{Gtk::Orientation::VERTICAL};
    /* Each channel's node editor, built the first time its graph is
       looked at. Building one scans the whole plugin directory for the
       palette, so sixteen of them up front would be sixteen scans for the
       one you wanted. */
    std::map<int, NodeEditor *> editors_;

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

    /* True while the pages are being taken away, and for good once the
       window is being destroyed: nothing is worth building then. */
    bool tearingDown_;

    /* The layout last kept, and the write that is waiting to happen. */
    string keptLayout_;

    /* Whether panes.ini had a layout when the window was built: if not,
       this is the first run with panes. */
    bool hadLayout_ = false;
    sigc::connection layoutWrite_;
private:
    gthAudio *audio_;

    /* The shipped tree, which is what the browser catalogs, and wherever a
       file chooser last was, which is not the same thing. */
    string dspDir_;
    string prevDir_;

    /* Last size seen on screen, for the preferences. Tracked as it changes
       rather than read at the end: by the time the window is being destroyed
       it has been hidden, and a hidden window's size is not a question with a
       reliable answer. */
    int width_, height_;

};

#endif /* MAIN_SYNTH_WINDOW_H */
