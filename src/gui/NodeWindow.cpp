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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* access -- can the source be written back? */

#include <fstream>

#include <gtkmm.h>

#include "think.h"

#include "../NodeGraph.h"
#include "../NodeLayout.h"
#include "../NodeEdit.h"
#include "NodeCanvas.h"
#include "NodeParams.h"
#include "NodePalette.h"
#include "../NodeCatalog.h"
#include "NodeWindow.h"

NodeWindow::NodeWindow (thSynth *synth)
    : synth_(synth), tree_(NULL), channel_(-1), layoutDirty_(false),
      structuralDirty_(false), selStatus_(false),
      newBtn_("New..."), deleteBtn_("Delete node"),
      arrangeBtn_("Auto-arrange"), saveBtn_("Save"), saveAsBtn_("Save As..."),
      revertBtn_("Revert"),
      zoomInBtn_("+"), zoomOutBtn_("-"), zoomResetBtn_("1:1"),
      zoomFitBtn_("Fit")
{
    set_title("thinksynth - Nodes");
    set_default_size(900, 600);

    add(vbox_);

    toolbar_.set_spacing(4);
    toolbar_.set_border_width(4);
    toolbar_.pack_start(newBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_start(deleteBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_start(arrangeBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_start(saveBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_start(saveAsBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_start(revertBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomInBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomResetBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomOutBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomFitBtn_, Gtk::PACK_SHRINK);

    scroller_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroller_.add(canvas_);

    /* Palette on the left, canvas in the middle, parameters on the right --
       the order things are used in: pick, place, adjust. */
    outer_.pack1(palette_, false, false);
    outer_.pack2(split_, true, false);
    outer_.set_position(210);

    split_.pack1(scroller_, true, false);
    split_.pack2(params_, false, false);
    split_.set_position(520);

    status_.set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    status_.set_padding(6, 2);

    vbox_.pack_start(toolbar_, Gtk::PACK_SHRINK);
    vbox_.pack_start(outer_);
    vbox_.pack_start(status_, Gtk::PACK_SHRINK);

    arrangeBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onArrange));
    saveBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onSave));
    saveAsBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onSaveAs));
    revertBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onRevert));
    zoomInBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomIn));
    zoomOutBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomOut));
    zoomResetBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomReset));
    zoomFitBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomFit));

    canvas_.signal_box_moved().connect(
        sigc::mem_fun(*this, &NodeWindow::onBoxMoved));

    canvas_.signal_selected().connect(
        sigc::mem_fun(*this, &NodeWindow::onSelected));

    canvas_.signal_selection().connect(
        sigc::mem_fun(*this, &NodeWindow::onSelectionChanged));

    canvas_.signal_connect_requested().connect(
        sigc::mem_fun(*this, &NodeWindow::onConnect));

    canvas_.signal_disconnect_requested().connect(
        sigc::mem_fun(*this, &NodeWindow::onDisconnect));

    canvas_.signal_refused().connect(
        sigc::mem_fun(*this, &NodeWindow::onRefused));

    canvas_.signal_control_changed().connect(
        sigc::mem_fun(*this, &NodeWindow::onControlChanged));

    palette_.signal_add().connect(
        sigc::mem_fun(*this, &NodeWindow::onPaletteAdd));

    palette_.signal_add_control().connect(
        sigc::mem_fun(*this, &NodeWindow::onPaletteAddControl));

    newBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onNewFile));
    deleteBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onDeleteNode));

    /* Where the synth is actually loading plugins from, not where the build
       expected them -- an uninstalled tree has them in ./plugins. Asking the
       manager means the palette can only ever offer what this synth can
       load. */
    thPluginManager *pm = synth_ ? synth_->getPluginManager() : NULL;

    palette_.populate(pm ? pm->pluginPath() : string(PLUGIN_PATH), pm);

    params_.signal_param_edited().connect(
        sigc::mem_fun(*this, &NodeWindow::onParamEdited));

    saveBtn_.set_sensitive(false);
    saveAsBtn_.set_sensitive(false);
    revertBtn_.set_sensitive(false);
    deleteBtn_.set_sensitive(false);
    palette_.setSensitive(false);

    show_all_children();
}

NodeWindow::~NodeWindow (void)
{
    /* parseTree() handed us a tree nobody else tracks. */
    delete tree_;

    /* The scratch copy is this window's alone and means nothing without it.
       Anything worth keeping went to the source on Save. */
    if (!work_.empty())
        ::remove(work_.c_str());
}

void NodeWindow::setStatus (const string &text)
{
    status_.set_text(text);

    /* Whatever this says now, it is not the selection count any more. */
    selStatus_ = false;
}

/* The status bar is shared: it reports what just happened -- saved, deleted,
 * could not parse -- and those messages are worth keeping on screen.
 *
 * "5 nodes selected" is a different kind of thing. It describes a state rather
 * than an event, so it has to go when that state does, and it stayed put after
 * deselecting because nothing owned it. These two make the ownership explicit:
 * the count is cleared only if the count is still what is showing, so
 * deselecting after a save does not wipe "Saved: 3 values". */
void NodeWindow::setSelectionStatus (const string &text)
{
    status_.set_text(text);
    selStatus_ = true;
}

void NodeWindow::clearSelectionStatus (void)
{
    if (!selStatus_)
        return;

    status_.set_text("");
    selStatus_ = false;
}

/* Is there anything the source does not have?
 *
 * Two kinds, and both count. Values, wires and positions are held in memory
 * until Save. Added and removed nodes are already in the working copy, because
 * showing them needed a parse -- but the source has not seen them either, so
 * structuralDirty_ makes them count the same. Before the working copy they
 * were in the user's file the moment they happened, so there was nothing to
 * track and Save could not be offered for them. */
bool NodeWindow::dirty (void) const
{
    return layoutDirty_ || structuralDirty_ || !pending_.empty() ||
           !wires_.empty() || !controls_.empty();
}

void NodeWindow::updateTitle (void)
{
    string base = thUtil::basename((char *)source_.c_str());

    /* A read-only source is worth saying in the title rather than only when
       Save is pressed: it changes where the work is going to end up. */
    const string ro = (!source_.empty() && !sourceWritable())
                      ? "  [read-only]" : "";

    set_title("thinksynth - Nodes - " + base + (dirty() ? " *" : "") + ro);
}

void NodeWindow::updateDirty (void)
{
    const bool dirty = this->dirty();

    /* Save is offered only when there is something to save. It used to be
       enabled whenever a file was open, and saving with nothing pending still
       wrote the layout block -- so opening a .dsp that had never been through
       the editor and pressing Save added twenty comment lines to a file the
       user had not edited. */
    saveBtn_.set_sensitive(!work_.empty() && dirty);

    /* Save As stays available with nothing pending: "give me my own copy of
       this patch" is a reason to use it, and for a read-only source it is the
       only way anything gets written at all. */
    saveAsBtn_.set_sensitive(!work_.empty());
    revertBtn_.set_sensitive(dirty);

    updateTitle();
}

bool NodeWindow::attached (void) const
{
    return synth_ != NULL && channel_ >= 0 &&
           synth_->getChannel(channel_) != NULL;
}

/* A control is a channel arg, and a channel arg is shared by every note
 * sounding on that channel -- notes copy the tree but chanarg references
 * resolve back through it. So this is heard immediately, on notes already
 * ringing, which is the whole point of putting sliders on the canvas.
 *
 * thArg::setValue(float) is the route the keyboard's own sliders use: a single
 * atomic store, no reallocation, safe to call from here while the audio thread
 * reads. */
const char *NodeWindow::applyControlLive (const string &name, double value)
{
    if (!attached())
        return "";

    thArg *arg = synth_->getChanArg(channel_, name);

    if (arg == NULL)
        return "  (not on the live channel)";

    arg->setValue((float)value);

    return "  (live)";
}

/* A node's own value is a different matter. Each note copy-constructs the
 * whole tree at note-on, so changing the channel's template changes what the
 * *next* note will sound like and leaves ringing notes alone. Saying so is
 * better than implying an immediacy that is not there. */
const char *NodeWindow::applyValueLive (const string &node, const string &arg,
                                        double value)
{
    if (!attached())
        return "";

    thMidiChan *chan = synth_->getChannel(channel_);
    thSynthTree *tree = chan ? chan->modnode() : NULL;

    if (tree == NULL)
        return "  (not on the live channel)";

    thNode *n = tree->findNode(node);
    thArg *a = n ? n->getArg(arg) : NULL;

    /* Only a plain value. Writing over an arg that points at another node or
       at a chanarg would break the link rather than change a number. */
    if (a == NULL || a->type() != thArg::ARG_VALUE)
        return "  (not on the live channel)";

    a->setValue((float)value);

    return "  (next note)";
}

bool NodeWindow::copyFile (const string &from, const string &to)
{
    ifstream in(from.c_str(), ios::binary);

    if (!in)
        return false;

    ofstream out(to.c_str(), ios::binary | ios::trunc);

    if (!out)
        return false;

    out << in.rdbuf();

    /* Both ends checked, and for badbit rather than goodness.
     *
     * failbit is not an error here: an empty source sets it on the insert
     * because rdbuf() extracted no characters, and an empty .dsp is a file
     * like any other. badbit is the one that means the stream broke.
     *
     * The read side matters as much as the write side and was not being
     * looked at. A source that fails part way through -- a disk giving up, a
     * file on a network mount that went away -- leaves a short but perfectly
     * well-formed working copy, and saying "copied" about it is how a patch
     * gets silently truncated on the next Save. */
    out.flush();

    return !in.bad() && !out.bad();
}

bool NodeWindow::sourceWritable (void) const
{
    if (source_.empty())
        return false;

    return access(source_.c_str(), W_OK) == 0;
}

/* A scratch .dsp holding a copy of the source.
 *
 * In the temp directory rather than beside the original, because the whole
 * point is that the original's directory may not be writable -- an installed
 * patch lives somewhere root owns. Nothing is lost by moving it: a .dsp
 * resolves no paths relative to itself. Strings in the grammar are only name,
 * author, description, label and group, and no plugin opens a file, so the
 * copy parses exactly as the original does wherever it sits.
 *
 * One scratch file per window, reused for the life of it and removed in the
 * destructor. */
bool NodeWindow::startWorkingCopy (const string &source)
{
    if (work_.empty())
    {
        /* mkstemp, not a name built from the pid and this pointer.
         *
         * That name was predictable, and copyFile opens with ios::trunc: on a
         * shared temp directory without the sticky bit, anyone able to guess
         * it could put a symlink there first and have the editor truncate
         * whatever it pointed at. mkstemp creates the file itself, O_EXCL and
         * 0600, so there is nothing to guess and nothing to pre-empt.
         *
         * No .dsp on the end -- mkstemp wants the template to finish with the
         * X's. Nothing cares: the parser is handed a path, not an extension,
         * and this file is never shown to anyone. */
        const char *tmp = getenv("TMPDIR");

        string tpl = string(tmp && *tmp ? tmp : "/tmp");

        tpl += "/thinksynth-edit-XXXXXX";

        vector<char> buf(tpl.begin(), tpl.end());

        buf.push_back('\0');

        const int fd = mkstemp(&buf[0]);

        if (fd < 0)
            return false;

        /* Closed rather than kept: everything downstream -- copyFile,
           NodeEdit, NodeLayout -- works by path, and NodeEdit deliberately
           writes through a temporary and renames, which replaces the inode
           under any fd we held anyway. The file stays ours and 0600 until the
           destructor removes it.

           ::close, because Gtk::Window has a close() of its own and this is a
           member function. */
        ::close(fd);

        work_ = &buf[0];
    }

    return copyFile(source, work_);
}

bool NodeWindow::open (const string &filename, int chan)
{
    /* Take the copy before anything else: from here on the window only ever
       looks at the copy, so a failure to make one has to stop the open. */
    if (!startWorkingCopy(filename))
    {
        setStatus("Could not read " + filename);
        return false;
    }

    const string prevSource = source_;
    const int prevChannel = channel_;

    source_ = filename;
    channel_ = chan;

    if (!reload())
    {
        source_ = prevSource;
        channel_ = prevChannel;
        return false;
    }

    structuralDirty_ = false;

    if (!sourceWritable())
        setStatus(status_.get_text() + "  --  read-only; Save will ask where "
                  "to put it");

    updateDirty();

    return true;
}

bool NodeWindow::reload (void)
{
    const string filename = work_;

    thSynthTree *tree = synth_->parseTree(filename);

    if (tree == NULL)
    {
        setStatus("Could not parse " + source_);
        return false;
    }

    NodeGraph g;

    if (!g.build(tree))
    {
        delete tree;
        setStatus("Could not build a graph for " + source_);
        return false;
    }

    g.layout();

    /* Saved positions override the computed ones, per box. A file with a
       partial layout block -- a node added by hand since the last save -- gets
       the laid-out position for the ones it does not mention, which is more
       useful than either ignoring the file or piling the strays at 0,0. */
    NodeLayout::PosMap pos;
    int restored = 0;

    if (NodeLayout::read(filename, pos) && !pos.empty())
        restored = NodeLayout::apply(g, pos, g);

    /* Swap in only once everything above has succeeded, so a failed open
       leaves the previous file on screen rather than a blank canvas. */
    delete tree_;
    tree_ = tree;
    graph_ = g;
    layoutDirty_ = false;
    pending_.clear();
    wires_.clear();
    controls_.clear();

    canvas_.setGraph(&graph_);

    /* Deliberately not zoomToFit() here. Opening every patch shrunk to fit
       makes a wide one unreadable and disguises the fact that it is wide,
       which is a way of not fixing it. Fit is a button for when an overview
       is what you want. */
    params_.setBox(NULL, -1);
    palette_.setSensitive(true);
    deleteBtn_.set_sensitive(false);
    updateDirty();     /* leaves Save insensitive: nothing is pending yet */

    char buf[256];

    char chanText[64] = "  not attached to a channel";

    if (attached())
        snprintf(chanText, sizeof(chanText), "  live on channel %d",
                 channel_ + 1);

    snprintf(buf, sizeof(buf),
             "%d nodes, %d connections (%d feedback), %d layers%s.%s",
             (int)graph_.boxes().size(), (int)graph_.edges().size(),
             graph_.feedbackCount(), graph_.layerCount(),
             restored ? ", layout restored" : "", chanText);

    setStatus(buf);

    return true;
}

void NodeWindow::onArrange (void)
{
    if (tree_ == NULL)
        return;

    graph_.layout();
    canvas_.setGraph(&graph_);

    /* Auto-arranging discards hand placement, so it is itself an edit. */
    layoutDirty_ = true;
    updateDirty();

    setStatus("Auto-arranged.");
}

/* Writes the pending values first, then the positions.
 *
 * Values first because NodeLayout::write rewrites the whole file to move its
 * comment block, and doing that between two value splices would mean the
 * second splice worked from a file the first had already reflowed. */
/* Saves anything held in memory, for the operations that work on the file.
 *
 * Adding and deleting both edit the .dsp text and then reopen it, and the
 * reopen throws away everything pending. So they have to flush first -- and,
 * if the flush fails, not go ahead. Each of the three call sites wrote this
 * out itself and each got it slightly wrong: two ignored the result entirely,
 * and the third reported the failure into a status bar that the success
 * message overwrote a moment later.
 *
 * Says nothing and returns true when there is nothing to write, so callers can
 * ask unconditionally. */
bool NodeWindow::flushPending (string &why)
{
    if (pending_.empty() && wires_.empty() && controls_.empty() &&
        !layoutDirty_)
        return true;

    return writeAll(why);
}

bool NodeWindow::writeAll (string &why)
{
    /* Wires before values: disconnecting puts a plain number in place of the
       connection, and a value edit on that same arg should land on top of it
       rather than being overwritten by it. */
    for (size_t w = 0; w < wires_.size(); w++)
    {
        const WireEdit &e = wires_[w];

        NodeEdit::Result r;

        if (!e.srcControl.empty())
            r = NodeEdit::connectControl(work_, e.node, e.arg,
                                         e.srcControl, why);
        else if (e.srcNode.empty())
            r = NodeEdit::disconnect(work_, e.node, e.arg, 0, why);
        else
            r = NodeEdit::connect(work_, e.node, e.arg,
                                  e.srcNode, e.srcPort, why);

        if (r != NodeEdit::OK)
        {
            if (why.empty())
                why = string(NodeEdit::resultText(r));

            why = e.node + "." + e.arg + ": " + why;

            return false;
        }
    }

    for (std::map<std::string, double>::const_iterator i = controls_.begin();
         i != controls_.end(); ++i)
    {
        NodeEdit::Result r =
            NodeEdit::setChanArg(work_, i->first, i->second, why);

        if (r != NodeEdit::OK)
        {
            if (why.empty())
                why = string(NodeEdit::resultText(r));

            why = "@" + i->first + ": " + why;

            return false;
        }
    }

    for (std::map<std::pair<std::string, std::string>, double>::const_iterator
             i = pending_.begin();
         i != pending_.end(); ++i)
    {
        NodeEdit::Result r = NodeEdit::setValue(work_, i->first.first,
                                                i->first.second, i->second,
                                                why);

        if (r != NodeEdit::OK)
        {
            if (why.empty())
                why = string(NodeEdit::resultText(r));

            why = i->first.first + "." + i->first.second + ": " + why;

            return false;
        }
    }

    if (!NodeLayout::write(work_, graph_))
    {
        why = "could not write the layout";
        return false;
    }

    return true;
}

void NodeWindow::onSave (void)
{
    if (work_.empty())
        return;

    const int n = (int)pending_.size();
    const int w = (int)wires_.size();
    const int c = (int)controls_.size();

    string why;

    if (!writeAll(why))
    {
        Gtk::MessageDialog dlg(*this, "Could not save.", false,
                               Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        dlg.set_secondary_text(why);
        dlg.run();

        setStatus("Not saved: " + why);
        return;
    }

    /* Everything is now in the working copy. Putting it where the user
       expects is a separate step, and the one that can fail for reasons that
       have nothing to do with the edits -- an installed patch, a read-only
       medium, someone else's file.

       Offer Save As rather than an error. The work is safe either way; all
       that is in question is where it lands. */
    if (!sourceWritable() || !copyFile(work_, source_))
    {
        if (!saveAsDialog())
        {
            setStatus("Not saved: " + source_ + " cannot be written. "
                      "Use Save As to put it somewhere else.");
            return;
        }
    }

    structuralDirty_ = false;

    /* Reparse rather than trusting the in-memory graph. The file is now the
       truth, and reading it back is the only way to see what it actually says
       -- including a value the writer had to round to something spellable.

       reload(), not open(): the working copy is what was just written and the
       source is now a copy of it, so there is nothing to fetch. */
    const int sel = canvas_.selected();

    if (!reload())
        return;

    if (sel >= 0 && sel < (int)graph_.boxes().size())
        canvas_.setSelected(sel);

    char buf[160];

    /* One snprintf per case rather than a format string chosen by a
       conditional. An earlier version handed the same argument list to both
       branches, so the one taking no arguments read an int as a %s. */
    if (n || c || w)
        snprintf(buf, sizeof(buf),
                 "Saved: %d value%s, %d control%s, %d wire change%s, "
                 "and the layout.",
                 n, n == 1 ? "" : "s", c, c == 1 ? "" : "s",
                 w, w == 1 ? "" : "s");
    else
        snprintf(buf, sizeof(buf), "Saved: layout only.");

    setStatus(buf);
}

void NodeWindow::onRevert (void)
{
    if (work_.empty())
        return;

    /* Remember what was touched before reloading, because reloading is what
       tells us the on-disk values to put back. */
    vector<string> ctlNames;

    for (std::map<std::string, double>::const_iterator i = controls_.begin();
         i != controls_.end(); ++i)
        ctlNames.push_back(i->first);

    vector<pair<string, string> > valNames;

    for (std::map<std::pair<std::string, std::string>, double>::const_iterator
             i = pending_.begin();
         i != pending_.end(); ++i)
        valNames.push_back(i->first);

    pending_.clear();
    wires_.clear();
    controls_.clear();
    layoutDirty_ = false;

    /* open(), not reload(): re-copying the source over the working file is
       what throws away nodes added and deleted since the last save. Until the
       working copy existed there was nothing to throw them away with, and
       Revert quietly left them in place. */
    if (!open(source_, channel_))
        return;

    /* Put the running synth back as well. Reverting the file and leaving the
       sound wherever the sliders were left would be worse than not offering
       Revert at all. */
    int restored = 0;

    for (size_t i = 0; i < ctlNames.size(); i++)
        for (size_t q = 0; q < graph_.boxes().size(); q++)
            if (graph_.boxes()[q].isControl &&
                graph_.boxes()[q].ctlArg == ctlNames[i])
            {
                applyControlLive(ctlNames[i], graph_.boxes()[q].ctlValue);
                restored++;
                break;
            }

    for (size_t i = 0; i < valNames.size(); i++)
    {
        const int b = graph_.boxByName(valNames[i].first);

        if (b < 0)
            continue;

        for (size_t k = 0; k < graph_.boxes()[b].params.size(); k++)
            if (graph_.boxes()[b].params[k].name == valNames[i].second &&
                graph_.boxes()[b].params[k].hasValue)
            {
                applyValueLive(valNames[i].first, valNames[i].second,
                               graph_.boxes()[b].params[k].value);
                restored++;
                break;
            }
    }

    char buf[128];

    if (attached() && restored)
        snprintf(buf, sizeof(buf),
                 "Reverted to what is on disk; %d value%s put back live.",
                 restored, restored == 1 ? "" : "s");
    else
        snprintf(buf, sizeof(buf), "Reverted to what is on disk.");

    setStatus(buf);
}

void NodeWindow::onBoxMoved (int box)
{
    (void)box;

    if (layoutDirty_)
        return;

    layoutDirty_ = true;
    updateDirty();
}

/* One box selected: the panel shows it, and Delete names what it would take.
 *
 * Controls are deletable now that they can be created. The io node is still
 * not: a .dsp without one does not load. */
void NodeWindow::onSelected (int box)
{
    params_.setBox(&graph_, box);

    /* A group is still a group when one of its members is pressed to drag it,
       and this fires for that. So ask the canvas how many are actually held
       rather than inferring it from `box' -- otherwise grabbing one node of
       five would clear the count and relabel the button while all five stay
       selected and Delete still takes all five.
    
       By the time this runs on a band release the selection is already the new
       one, so the count is the right question to ask. */
    if (canvas_.selection().size() > 1)
        return;

    clearSelectionStatus();

    const bool deletable =
        box >= 0 && box < (int)graph_.boxes().size() &&
        !graph_.boxes()[box].isIoSource && !graph_.boxes()[box].isIoSink;

    deleteBtn_.set_sensitive(deletable);

    /* The label needs resetting when nothing is selected too. It was only set
       for a real box, so after a group it stayed on "Delete 5" -- greyed out,
       but still offering to remove five things nobody had chosen any more. */
    if (box >= 0 && box < (int)graph_.boxes().size())
        deleteBtn_.set_label(graph_.boxes()[box].isControl ? "Delete control"
                                                           : "Delete node");
    else
        deleteBtn_.set_label("Delete node");
}

/* A rubber band gathered `n' boxes.
 *
 * onSelected has already run with -1 for anything but a single box, which
 * emptied the panel and disabled Delete. This puts Delete back for a group and
 * says how many it would take, so the count is visible before pressing it
 * rather than only in the status bar afterwards. */
void NodeWindow::onSelectionChanged (int n)
{
    if (n <= 1)
        return;             /* onSelected has it */

    /* Whether any of them can actually go. A band across the right-hand end
       of a patch catches the audio output, and a selection of nothing but the
       io halves has nothing to delete. */
    int deletable = 0;

    const vector<int> &sel = canvas_.selection();

    for (size_t i = 0; i < sel.size(); i++)
        if (sel[i] >= 0 && sel[i] < (int)graph_.boxes().size() &&
            !graph_.boxes()[sel[i]].isIoSource &&
            !graph_.boxes()[sel[i]].isIoSink)
            deletable++;

    deleteBtn_.set_sensitive(deletable > 0);

    char buf[48];

    snprintf(buf, sizeof(buf), "Delete %d", deletable);
    deleteBtn_.set_label(buf);

    snprintf(buf, sizeof(buf), "%d nodes selected.", (int)sel.size());
    setSelectionStatus(buf);
}

vector<string> NodeWindow::takenNames (void) const
{
    vector<string> names;

    for (size_t b = 0; b < graph_.boxes().size(); b++)
        if (!graph_.boxes()[b].isControl)
            names.push_back(graph_.boxes()[b].name);

    return names;
}

/* Adds a node of the chosen type, named so it does not collide.
 *
 * Written out at once rather than held with the other pending edits. A node's
 * ports come from its plugin, and the only thing that knows them is a parse --
 * so the honest way to show a new node is to write it and reparse.
 *
 * What it is written to is the working copy, not the user's .dsp. That is what
 * makes the claim in the next sentence true, which it was not before: Revert
 * undoes this like anything else, by taking a fresh copy of the source. */
void NodeWindow::onPaletteAdd (string spelling)
{
    if (work_.empty())
    {
        setStatus("Open or create a .dsp first.");
        return;
    }

    const string name =
        NodeCatalog::suggestName(spelling.substr(spelling.find("::") + 2),
                                 takenNames());

    string why;

    /* Flush before touching the file, not after. Everything pending lives in
       memory and the reopen below would drop it, so it has to go out first --
       and if it cannot, the add must not happen either. Writing the node and
       then failing to save would leave the file holding the new node and none
       of the edits, which is a state the user never asked for and cannot
       undo. */
    if (!flushPending(why))
    {
        setStatus("Not adding " + spelling + ": the pending edits could not be "
                  "saved first (" + why + ")");
        return;
    }

    NodeEdit::Result r = NodeEdit::addNode(work_, name, spelling, why);

    if (r != NodeEdit::OK)
    {
        setStatus("Could not add " + spelling + ": " + why);
        return;
    }

    structuralDirty_ = true;

    /* reload(), emphatically not open(): the node has just been written to
       the working copy, and open() would replace that copy with a fresh one
       taken from the source -- undoing the add in the act of displaying it. */
    if (!reload())
        return;

    /* Put it where there is room rather than on top of layer 0, and select it
       so its parameters are the ones showing. */
    const int box = graph_.boxByName(name);

    if (box >= 0)
    {
        graph_.moveBox(box, 20.0, graph_.height() + 20.0);
        graph_.refreshExtent();
        canvas_.setGraph(&graph_);
        canvas_.setSelected(box);

        layoutDirty_ = true;
        updateDirty();
    }

    setStatus("Added " + name + " (" + spelling + ").  Wire it up, then Save.");
}

/* Name, range and label for a new control.
 *
 * A control needs more than a click: unlike a plugin, nothing about it is
 * implied by what was chosen. The range in particular has no sensible default
 * -- 0 to 1 is right for a mix and useless for a filter cutoff -- and getting
 * it wrong means a slider that cannot reach the value you want. */
bool NodeWindow::askControl (string &name, double &value, double &min,
                             double &max, string &label)
{
    Gtk::Dialog dlg("New control", *this, true);

    dlg.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dlg.add_button("Add", Gtk::RESPONSE_OK);
    dlg.set_default_response(Gtk::RESPONSE_OK);

    Gtk::Grid grid;

    grid.set_row_spacing(4);
    grid.set_column_spacing(8);
    grid.set_border_width(8);

    Gtk::Entry nameEntry, labelEntry;

    nameEntry.set_text(name);
    nameEntry.set_activates_default(true);
    labelEntry.set_activates_default(true);
    labelEntry.set_placeholder_text("optional");

    Glib::RefPtr<Gtk::Adjustment> minAdj =
        Gtk::Adjustment::create(0, -1000000, 1000000, 0.1, 1);
    Glib::RefPtr<Gtk::Adjustment> maxAdj =
        Gtk::Adjustment::create(1, -1000000, 1000000, 0.1, 1);
    Glib::RefPtr<Gtk::Adjustment> valAdj =
        Gtk::Adjustment::create(0.5, -1000000, 1000000, 0.1, 1);

    Gtk::SpinButton minSpin(minAdj, 0.1, 4);
    Gtk::SpinButton maxSpin(maxAdj, 0.1, 4);
    Gtk::SpinButton valSpin(valAdj, 0.1, 4);

    const char *labels[] = { "Name", "Label", "Minimum", "Maximum", "Value" };
    Gtk::Widget *fields[] = { &nameEntry, &labelEntry, &minSpin, &maxSpin,
                              &valSpin };

    for (int i = 0; i < 5; i++)
    {
        Gtk::Label *l = manage(new Gtk::Label(labels[i]));

        l->set_alignment(Gtk::ALIGN_END, Gtk::ALIGN_CENTER);

        grid.attach(*l, 0, i, 1, 1);
        grid.attach(*fields[i], 1, i, 1, 1);
    }

    Gtk::Label hint;

    hint.set_markup("<small>The name is what nodes read as "
                    "<tt>@name</tt>.</small>");
    hint.set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    grid.attach(hint, 0, 5, 2, 1);

    dlg.get_content_area()->pack_start(grid);
    dlg.show_all_children();

    /* Looped rather than validated once, so a rejected name can be corrected
       instead of throwing the whole dialog away. */
    while (dlg.run() == Gtk::RESPONSE_OK)
    {
        name = nameEntry.get_text();
        label = labelEntry.get_text();
        min = minSpin.get_value();
        max = maxSpin.get_value();
        value = valSpin.get_value();

        string complaint;

        if (!NodeEdit::validName(name))
            complaint = "A control name must start with a letter or "
                        "underscore and contain only letters, digits and "
                        "underscores.";
        else if (!NodeEdit::validLabel(label))
            complaint = "A label cannot contain a quote -- the .dsp string "
                        "syntax has no way to escape one.";
        else if (max <= min)
            complaint = "The maximum must be above the minimum.";

        if (complaint.empty())
            return true;

        Gtk::MessageDialog err(dlg, complaint, false, Gtk::MESSAGE_WARNING,
                               Gtk::BUTTONS_OK, true);
        err.run();
    }

    return false;
}

void NodeWindow::onPaletteAddControl (void)
{
    if (work_.empty())
    {
        setStatus("Open or create a .dsp first.");
        return;
    }

    /* A name nothing else uses, offered as a starting point. */
    vector<string> taken;

    for (size_t b = 0; b < graph_.boxes().size(); b++)
        if (graph_.boxes()[b].isControl)
            taken.push_back(graph_.boxes()[b].ctlArg);

    string name = NodeCatalog::suggestName("ctl", taken);
    string label;
    double value = 0.5, min = 0, max = 1;

    if (!askControl(name, value, min, max, label))
        return;

    string why;

    /* Same order as adding a node: flush first, and give up if it fails,
       rather than writing the control into a file that has lost every other
       edit. */
    if (!flushPending(why))
    {
        setStatus("Not adding @" + name + ": the pending edits could not be "
                  "saved first (" + why + ")");
        return;
    }

    if (NodeEdit::addControl(work_, name, value, min, max, label, why)
        != NodeEdit::OK)
    {
        setStatus("Could not add @" + name + ": " + why);
        return;
    }

    structuralDirty_ = true;

    if (!reload())
        return;

    const int box = graph_.boxByName("@" + name);

    if (box >= 0)
    {
        graph_.moveBox(box, 20.0, graph_.height() + 20.0);
        graph_.refreshExtent();
        canvas_.setGraph(&graph_);
        canvas_.setSelected(box);

        layoutDirty_ = true;
        updateDirty();
    }

    setStatus("Added @" + name +
              ".  Drag from its output to a parameter to use it.");
}

/* Deletes everything selected.
 *
 * Names are collected before anything is removed, because removing the first
 * one reparses and every box index after it means something different. A name
 * survives that; an index does not.
 *
 * The io halves are skipped rather than refused: a rubber band across the
 * right-hand side of a patch will catch the audio output, and there is no
 * reading of "delete these six nodes" under which the user meant to be told
 * off about the one that cannot go. A .dsp without an io node does not load,
 * so it simply stays. */
void NodeWindow::onDeleteNode (void)
{
    if (work_.empty())
        return;

    vector<pair<string, bool> > victims;   /* name, isControl */
    int skipped = 0;

    const vector<int> &sel = canvas_.selection();

    for (size_t i = 0; i < sel.size(); i++)
    {
        if (sel[i] < 0 || sel[i] >= (int)graph_.boxes().size())
            continue;

        const NodeGraph::Box &sb = graph_.boxes()[sel[i]];

        if (sb.isIoSource || sb.isIoSink)
        { skipped++; continue; }

        victims.push_back(make_pair(sb.isControl ? sb.ctlArg : sb.name,
                                    sb.isControl));
    }

    if (victims.empty())
        return;

    const bool isControl = victims[0].second;
    const string name = victims[0].first;

    string why;
    int refs = 0;

    /* Flush first, because remove works on the file and the pending edits are
       not in it yet.

       If that write fails, stop. Going on would delete from a file missing
       every edit since the last save, then reopen and report success -- the
       edits gone and nothing said about it. A delete the user can retry after
       fixing the disk is much better than one that quietly takes the wrong
       file with it. */
    if (!flushPending(why))
    {
        setStatus("Not deleting " + name + ": the pending edits could not be "
                  "saved first (" + why + ")");
        return;
    }

    int done = 0, failedAt = -1;

    for (size_t i = 0; i < victims.size(); i++)
    {
        int n = 0;

        const NodeEdit::Result r =
            victims[i].second
                ? NodeEdit::removeControl(work_, victims[i].first, n, why)
                : NodeEdit::removeNode(work_, victims[i].first, n, why);

        if (r != NodeEdit::OK)
        { failedAt = (int)i; break; }

        refs += n;
        done++;
    }

    /* Anything removed at all is a change to the working copy, including a
       run that stopped part way. Marking it dirty is what makes Revert able
       to undo a half-finished delete -- which is the reason this can afford
       to stop at the first failure rather than trying to unpick itself. */
    if (done)
        structuralDirty_ = true;

    if (!done)
    {
        /* With the @ back on, as the success path has it. Controls are
           stored without one -- that is the spelling NodeEdit wants -- but a
           control and a node can share a name, so a message naming a bare
           `cutoff' does not say which of them would not go. */
        setStatus("Could not delete " +
                  string(victims[0].second ? "@" : "") + victims[0].first +
                  ": " + why);
        return;
    }

    if (!reload())
        return;

    char buf[256];

    const string shown = (isControl ? "@" : "") + name;

    /* The references are the part nobody asked for, so they are the part
       worth reporting. Left in place they would make the file load with
       "Node x not found" and read zero. */
    if (failedAt >= 0)
        snprintf(buf, sizeof(buf),
                 "Deleted %d of %d, then stopped at %s: %s.  Revert undoes "
                 "the rest.",
                 done, (int)victims.size(),
                 (string(victims[failedAt].second ? "@" : "") +
                  victims[failedAt].first).c_str(),
                 why.c_str());
    else if (done == 1 && refs)
        snprintf(buf, sizeof(buf),
                 "Deleted %s, and disconnected %d input%s that read from it.",
                 shown.c_str(), refs, refs == 1 ? "" : "s");
    else if (done == 1)
        snprintf(buf, sizeof(buf), "Deleted %s.%s", shown.c_str(),
                 skipped ? "  The io node stays." : "");
    else
        snprintf(buf, sizeof(buf),
                 "Deleted %d nodes, and disconnected %d input%s that read "
                 "from them.%s",
                 done, refs, refs == 1 ? "" : "s",
                 skipped ? "  The io node stays." : "");

    setStatus(buf);
}

/* A new .dsp: ask where, write the smallest file that loads, open it. */
/* Where should this go? Asks, writes there, and adopts it as the source.
 *
 * The working copy is already complete by the time this runs -- Save flushes
 * everything into it before deciding where to put it -- so this is a file copy
 * and a change of address, nothing more.
 *
 * Adopting the new path matters: after saving a read-only patch somewhere of
 * your own, the next Save should go there without asking again. */
bool NodeWindow::saveAsDialog (void)
{
    Gtk::FileChooserDialog dlg(*this, "Save DSP As",
                               Gtk::FILE_CHOOSER_ACTION_SAVE);

    dlg.set_transient_for(*this);
    dlg.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dlg.add_button("Save", Gtk::RESPONSE_OK);
    dlg.set_do_overwrite_confirmation(true);

    if (!source_.empty())
    {
        dlg.set_current_name(thUtil::basename((char *)source_.c_str()));

        /* Not the source's own folder as the starting point when it cannot be
           written -- offering the directory that just refused the write is
           not much of a suggestion. */
        if (sourceWritable())
            dlg.set_current_folder(thUtil::dirname(source_.c_str()));
    }
    else
        dlg.set_current_name("untitled.dsp");

    if (dlg.run() != Gtk::RESPONSE_OK)
        return false;

    const string path = dlg.get_filename();

    dlg.hide();

    if (!copyFile(work_, path))
    {
        Gtk::MessageDialog err(*this, "Could not save there.", false,
                               Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        err.set_secondary_text(path);
        err.run();

        return false;
    }

    source_ = path;

    return true;
}

void NodeWindow::onSaveAs (void)
{
    if (work_.empty())
        return;

    string why;

    /* Flush first: Save As means "this, as I see it, over there", and the
       pending values are part of what is on screen. */
    if (!flushPending(why))
    {
        setStatus("Not saved: " + why);
        return;
    }

    if (!saveAsDialog())
        return;

    structuralDirty_ = false;

    const int sel = canvas_.selected();

    if (!reload())
        return;

    if (sel >= 0 && sel < (int)graph_.boxes().size())
        canvas_.setSelected(sel);

    setStatus("Saved as " + source_ + ".");
}

void NodeWindow::onNewFile (void)
{
    Gtk::FileChooserDialog dlg(*this, "New DSP", Gtk::FILE_CHOOSER_ACTION_SAVE);

    dlg.set_transient_for(*this);
    dlg.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dlg.add_button("Create", Gtk::RESPONSE_OK);
    dlg.set_do_overwrite_confirmation(true);
    dlg.set_current_name("untitled.dsp");

    if (!source_.empty())
        dlg.set_current_folder(thUtil::dirname(source_.c_str()));

    if (dlg.run() != Gtk::RESPONSE_OK)
        return;

    const string path = dlg.get_filename();

    dlg.hide();

    string base = thUtil::basename(path.c_str());

    const string::size_type dot = base.rfind(".dsp");

    if (dot != string::npos)
        base = base.substr(0, dot);

    string why;

    /* The chooser has already confirmed the overwrite, so say so and let
       createFile do the replacing.

       This used to ::remove(path) first, to get past createFile's refusal to
       overwrite. That deleted the user's existing .dsp before finding out
       whether the new one could be written -- so a full disk or a read-only
       directory took the old file with it and left nothing behind. createFile
       writes a temporary and renames, so the old file is now replaced only
       once the new one is complete. */
    if (NodeEdit::createFile(path, base, "", true, why) != NodeEdit::OK)
    {
        Gtk::MessageDialog err(*this, "Could not create the file.", false,
                               Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        err.set_secondary_text(why);
        err.run();
        return;
    }

    /* Not attached to a channel: nothing has loaded it. */
    if (open(path, -1))
        setStatus("Created " + base +
                  ".dsp -- an io node and nothing else. Add nodes from the "
                  "palette.");
}

/* Records an edit. Nothing reaches the file until Save.
 *
 * Keyed by node and arg name rather than by box index so it survives the
 * reparse a save does. */
void NodeWindow::onParamEdited (int box, string name, double value)
{
    if (box < 0 || box >= (int)graph_.boxes().size())
        return;

    const NodeGraph::Box &b = graph_.boxes()[box];

    /* Typing a value back to what it already was is not an edit, and should
       not leave the window looking modified. */
    for (size_t k = 0; k < b.params.size(); k++)
        if (b.params[k].name == name &&
            b.params[k].hasValue && (float)b.params[k].value == (float)value)
        {
            pending_.erase(make_pair(b.name, name));
            updateDirty();
            setStatus("");
            return;
        }

    pending_[make_pair(b.name, name)] = value;

    const char *live = applyValueLive(b.name, name, value);

    updateDirty();

    char buf[256];

    snprintf(buf, sizeof(buf), "%s.%s = %g%s  (%d unsaved change%s)",
             b.name.c_str(), name.c_str(), value, live,
             (int)pending_.size(), pending_.size() == 1 ? "" : "s");

    setStatus(buf);
}

/* Applies a wire to the graph on screen and records it for the save.
 *
 * Applied immediately rather than only on Save so the canvas shows what was
 * just drawn -- a wiring gesture that produced no visible wire until a save
 * would be unusable. The file is still untouched until Save. */
void NodeWindow::onConnect (int fromBox, int fromPort, int toBox, int toPort)
{
    string why;

    if (!graph_.connect(fromBox, fromPort, toBox, toPort, why))
    {
        setStatus("Cannot connect: " + why);
        return;
    }

    WireEdit e;

    e.node = graph_.boxes()[toBox].name;
    e.arg = graph_.boxes()[toBox].ports[toPort].name;
    if (graph_.boxes()[fromBox].isControl)
        e.srcControl = graph_.boxes()[fromBox].ctlArg;
    else
    {
        e.srcNode = graph_.boxes()[fromBox].name;
        e.srcPort = graph_.boxes()[fromBox].ports[fromPort].name;
    }

    wires_.push_back(e);

    canvas_.queue_draw();
    params_.setBox(&graph_, canvas_.selected());
    updateDirty();

    setStatus(e.node + "." + e.arg + " <- " +
              (e.srcControl.empty() ? e.srcNode + "->" + e.srcPort
                                    : "@" + e.srcControl));
}

void NodeWindow::onDisconnect (int edge)
{
    if (edge < 0 || edge >= (int)graph_.edges().size())
        return;

    const NodeGraph::Edge &ed = graph_.edges()[edge];

    WireEdit e;

    e.node = graph_.boxes()[ed.toBox].name;
    e.arg = graph_.boxes()[ed.toBox].ports[ed.toPort].name;

    graph_.removeEdge(edge);

    wires_.push_back(e);

    canvas_.queue_draw();
    params_.setBox(&graph_, canvas_.selected());
    updateDirty();

    setStatus("Disconnected " + e.node + "." + e.arg + ".  Revert undoes it.");
}

/* A slider moved. `commit' is false while dragging and true on release.
 *
 * The graph has already been updated by the canvas, so this only has to
 * refresh what else is showing the number and, on release, record the edit.
 * Recording only on release means a drag across the track is one change
 * rather than one per pixel. */
void NodeWindow::onControlChanged (int box, double value, bool commit)
{
    if (box < 0 || box >= (int)graph_.boxes().size())
        return;

    const NodeGraph::Box &b = graph_.boxes()[box];

    /* Everything reading this control shows the new number too. */
    params_.setBox(&graph_, canvas_.selected());

    const char *live = applyControlLive(b.ctlArg, value);

    char buf[192];

    snprintf(buf, sizeof(buf), "@%s = %g%s", b.ctlArg.c_str(), value, live);

    setStatus(buf);

    if (!commit)
        return;

    controls_[b.ctlArg] = value;

    updateDirty();
}

void NodeWindow::onRefused (string why)
{
    setStatus("Cannot connect: " + why);
}

void NodeWindow::onZoomIn (void)
{
    canvas_.setZoom(canvas_.zoom() * 1.25);
}

void NodeWindow::onZoomOut (void)
{
    canvas_.setZoom(canvas_.zoom() / 1.25);
}

void NodeWindow::onZoomReset (void)
{
    canvas_.setZoom(1.0);
}

void NodeWindow::onZoomFit (void)
{
    canvas_.zoomToFit();
}
