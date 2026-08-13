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

#include <filesystem>
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
#include "NodeEditor.h"

#include "thUtil.h"
#include "Dialogs.h"


NodeEditor::NodeEditor (thSynth *synth)
    : Gtk::Box(Gtk::Orientation::VERTICAL),
      synth_(synth), tree_(NULL), channel_(-1), layoutDirty_(false),
      structuralDirty_(false), selStatus_(false),
      newBtn_("New..."), deleteBtn_("Delete node"),
      arrangeBtn_("Auto-arrange"), saveBtn_("Save"), saveAsBtn_("Save As..."),
      revertBtn_("Revert"),
      zoomInBtn_("+"), zoomOutBtn_("-"), zoomResetBtn_("1:1"),
      zoomFitBtn_("Fit"),
      paletteBtn_("Palette"), paramsBtn_("Parameters"),
      paletteWidth_(210), paramsWidth_(240), pendingParams_(-1)
{
    toolbar_.set_spacing(4);
    toolbar_.set_margin_start(4);
    toolbar_.set_margin_end(4);
    toolbar_.set_margin_top(4);
    toolbar_.set_margin_bottom(4);
    /* Where the window title used to be. A widget has no title bar, and the
       filename and the dirty marker still have to be somewhere -- they say
       which file every button on this bar would act on. */
    titleLbl_.set_xalign(0.0);
    titleLbl_.set_ellipsize(Pango::EllipsizeMode::START);
    titleLbl_.set_width_chars(18);
    titleLbl_.set_max_width_chars(40);

    /* A box packs in one direction now -- there is no pack_end -- so the
       right-hand group is reached by appending everything in the order it is
       read and putting an expanding nothing between the two halves. That is
       the widget GTK4 leaves you for the job, and it is at least honest about
       what pack_end was doing. */
    toolbar_.append(titleLbl_);
    toolbar_.append(*manage(new Gtk::Separator(Gtk::Orientation::VERTICAL)));
    toolbar_.append(newBtn_);
    toolbar_.append(deleteBtn_);
    toolbar_.append(arrangeBtn_);
    toolbar_.append(saveBtn_);
    toolbar_.append(saveAsBtn_);
    toolbar_.append(revertBtn_);

    {
        Gtk::Box *gap = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));

        gap->set_hexpand(true);
        toolbar_.append(*gap);
    }

    /* Grouped with the zoom buttons rather than with the editing ones: what
       is on screen and how big it is are the same kind of decision, and none
       of them change the file. */
    toolbar_.append(paletteBtn_);
    toolbar_.append(paramsBtn_);
    toolbar_.append(*manage(new Gtk::Separator(Gtk::Orientation::VERTICAL)));
    toolbar_.append(zoomFitBtn_);
    toolbar_.append(zoomOutBtn_);
    toolbar_.append(zoomResetBtn_);
    toolbar_.append(zoomInBtn_);

    paletteBtn_.set_active(true);
    paramsBtn_.set_active(true);
    paletteBtn_.set_tooltip_text("Show or hide the plugin palette");
    paramsBtn_.set_tooltip_text("Show or hide the parameter panel");

    scroller_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_child(canvas_);

    /* Palette on the left, canvas in the middle, parameters on the right --
       the order things are used in: pick, place, adjust. */
    /* pack1/pack2 with their resize and shrink flags are gone; a GTK4 paned
       takes a child for each side and the two flags are properties on it.
       resize=false, shrink=false for the palette is set_resize_start_child
       and set_shrink_start_child both off. */
    outer_.set_start_child(palette_);
    outer_.set_resize_start_child(false);
    outer_.set_shrink_start_child(false);
    outer_.set_end_child(split_);
    outer_.set_resize_end_child(true);
    outer_.set_shrink_end_child(false);
    outer_.set_position(paletteWidth_);

    split_.set_start_child(scroller_);
    split_.set_resize_start_child(true);
    split_.set_shrink_start_child(false);
    split_.set_end_child(params_);
    split_.set_resize_end_child(false);
    split_.set_shrink_end_child(false);

    /* No starting position here. It used to be 520 -- measured from the left,
       so it said "the canvas gets 520 pixels" and handed the parameter panel
       everything else, which on a wide window was most of it. What it should
       say is that the panel gets the width it asks for; that needs the paned's
       own width, so it waits for the first allocation. */
    split_.property_position().signal_changed().connect(
        sigc::mem_fun(*this, &NodeEditor::onSplitAllocate));
    split_.signal_map().connect(
        sigc::mem_fun(*this, &NodeEditor::onSplitAllocate));

    status_.set_xalign(0.0);
    status_.set_margin_start(6);
    status_.set_margin_end(6);
    status_.set_margin_top(2);
    status_.set_margin_bottom(2);

    append(toolbar_);
    outer_.set_vexpand(true);
    append(outer_);
    append(status_);

    arrangeBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onArrange));
    saveBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onSave));
    saveAsBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onSaveAs));
    revertBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onRevert));
    zoomInBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onZoomIn));
    zoomOutBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onZoomOut));
    zoomResetBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onZoomReset));
    zoomFitBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onZoomFit));

    canvas_.signal_box_moved().connect(
        sigc::mem_fun(*this, &NodeEditor::onBoxMoved));

    canvas_.signal_selected().connect(
        sigc::mem_fun(*this, &NodeEditor::onSelected));

    canvas_.signal_selection().connect(
        sigc::mem_fun(*this, &NodeEditor::onSelectionChanged));

    canvas_.signal_connect_requested().connect(
        sigc::mem_fun(*this, &NodeEditor::onConnect));

    canvas_.signal_disconnect_requested().connect(
        sigc::mem_fun(*this, &NodeEditor::onDisconnect));

    canvas_.signal_refused().connect(
        sigc::mem_fun(*this, &NodeEditor::onRefused));

    canvas_.signal_control_changed().connect(
        sigc::mem_fun(*this, &NodeEditor::onControlChanged));

    palette_.signal_add().connect(
        sigc::mem_fun(*this, &NodeEditor::onPaletteAdd));

    palette_.signal_add_control().connect(
        sigc::mem_fun(*this, &NodeEditor::onPaletteAddControl));

    newBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onNewFile));
    deleteBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeEditor::onDeleteNode));

    paletteBtn_.signal_toggled().connect(
        sigc::mem_fun(*this, &NodeEditor::onTogglePalette));
    paramsBtn_.signal_toggled().connect(
        sigc::mem_fun(*this, &NodeEditor::onToggleParams));

    /* Where the synth is actually loading plugins from, not where the build
       expected them -- an uninstalled tree has them in ./plugins. Asking the
       manager means the palette can only ever offer what this synth can
       load. */
    thPluginManager *pm = synth_ ? synth_->getPluginManager() : NULL;

    palette_.populate(pm ? pm->pluginPath() : string(PLUGIN_PATH), pm);

    params_.signal_param_edited().connect(
        sigc::mem_fun(*this, &NodeEditor::onParamEdited));

    /* Probes. The modules are loaded once here and shared by every probe that
       names one; the canvas is told how to get from a panel to the instance
       that draws in it. */
    scanVisuals();

    canvas_.setProbePainter(sigc::mem_fun(*this, &NodeEditor::paintProbe));
    canvas_.signal_context_requested().connect(
        sigc::mem_fun(*this, &NodeEditor::onContextRequested));
    canvas_.signal_probe_activated().connect(
        sigc::mem_fun(*this, &NodeEditor::onProbeActivated));

    saveBtn_.set_sensitive(false);
    saveAsBtn_.set_sensitive(false);
    revertBtn_.set_sensitive(false);
    deleteBtn_.set_sensitive(false);
    palette_.setSensitive(false);

}

/* GtkPaned's position is the width of its *left* child, so everything about
   the right one has to be expressed the long way round. */
/* The handle between the two children.
 *
 * It was a style property, which GTK4 removed along with the rest of them --
 * widget geometry comes from CSS now and there is no getter for it. The value
 * is the same one GTK3 defaulted to, and being a few pixels out only moves
 * the panel edge by that much. */
static const int PANED_HANDLE = 5;

int NodeEditor::paramsWidth (void) const
{
    return split_.get_width() - split_.get_position() - PANED_HANDLE;
}

void NodeEditor::setParamsWidth (int width)
{
    split_.set_position(split_.get_width() - width - PANED_HANDLE);
}

/* Hands the parameter panel whatever width it is owed, now that there is a
 * real one to measure against.
 *
 * What it is owed at startup is its own minimum: what it needs to show the
 * node that is selected, a name column and a value column, with nothing in it
 * wanting to be wider than its contents. That minimum is also the narrowest
 * the splitter can be dragged to, and it is the right place to start, because
 * anything beyond it is width taken off the canvas to show blank panel. The
 * position used to be a fixed 520 measured from the left, which said how much
 * the canvas got and gave the panel all the rest.
 *
 * The minimum rather than the natural width, deliberately. Both are honoured
 * by the paned, but the natural width is what a widget would like if space
 * were free, and here it is not -- it comes out of the graph. Selecting a node
 * whose parameters genuinely need more room still widens the panel, because
 * GtkPaned will not allocate a child less than its minimum. That is the one
 * case where taking the space is warranted, and it happens by itself.
 *
 * Afterwards position_set is on and the panel stays where it is put --
 * including where the user drags it to. */
void NodeEditor::onSplitAllocate (void)
{
    int want = pendingParams_;

    if (want == 0 || split_.get_width() < 2)
        return;

    if (want < 0)
    {
        /* get_preferred_width() is gone; measure() answers the same question
           for either orientation, and wants somewhere to put the baselines it
           has no opinion about here. */
        int nat = 0, minBaseline = 0, natBaseline = 0;

        params_.measure(Gtk::Orientation::HORIZONTAL, -1, want, nat,
                        minBaseline, natBaseline);
        paramsWidth_ = want;
    }

    pendingParams_ = 0;

    setParamsWidth(want);
}

/* Collapsing either panel gives its space to the canvas: a paned with one
   visible child allocates all of itself to that child, so there is nothing to
   do beyond hiding it. Expanding has to put back a width, which is why one is
   remembered on the way out. */
void NodeEditor::onTogglePalette (void)
{
    if (paletteBtn_.get_active())
    {
        palette_.show();
        outer_.set_position(paletteWidth_);
    }
    else
    {
        paletteWidth_ = outer_.get_position();
        palette_.hide();
    }
}

void NodeEditor::onToggleParams (void)
{
    if (paramsBtn_.get_active())
    {
        params_.show();

        /* Not set here: showing the panel is itself a resize, so the paned's
           width is about to change and the position that expresses this width
           would be computed against the old one. */
        pendingParams_ = paramsWidth_;
    }
    else
    {
        paramsWidth_ = paramsWidth();
        params_.hide();
    }
}

NodeEditor::~NodeEditor (void)
{
    /* Instances before modules: closing one calls into the .so that is about
       to be dlclosed. The tick has to stop first for the same reason -- a
       timer that fires during teardown would drain a tap into a closed
       instance. */
    probeTick_.disconnect();

    /* Windows first: each one draws through an instance that disarmAllProbes
       is about to close. */
    closeAllEnlarged();

    disarmAllProbes();

    for (std::map<std::string, thVisual *>::iterator v = visuals_.begin();
         v != visuals_.end(); ++v)
        delete v->second;

    visuals_.clear();

    /* A popover with a parent set outlives its parent otherwise, and GTK
       complains loudly at teardown. */
    if (ctxPopover_.get_parent())
        ctxPopover_.unparent();

    /* parseTree() handed us a tree nobody else tracks. */
    delete tree_;

    /* The scratch copy is this window's alone and means nothing without it.
       Anything worth keeping went to the source on Save. */
    if (!work_.empty())
        ::remove(work_.c_str());
}

void NodeEditor::setStatus (const string &text)
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
void NodeEditor::setSelectionStatus (const string &text)
{
    status_.set_text(text);
    selStatus_ = true;
}

void NodeEditor::clearSelectionStatus (void)
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
bool NodeEditor::dirty (void) const
{
    return layoutDirty_ || structuralDirty_ || !pending_.empty() ||
           !wires_.empty() || !controls_.empty();
}

/* The window this is packed into.
 *
 * get_toplevel() returns the widget itself when it has no parent yet, so the
 * cast is the check: a dialog asked for before this is in a window gets no
 * parent rather than a wrong one. */
Gtk::Window *NodeEditor::topLevel (void)
{
    /* get_toplevel() is gone; the root of the widget tree is what it was
       reaching for, and it answers NULL rather than the widget itself when
       there is not one yet -- which is the check this wanted. */
    return dynamic_cast<Gtk::Window *>(get_root());
}

void NodeEditor::updateTitle (void)
{
    string base = thUtil::basename(source_.c_str());

    /* A read-only source is worth saying in the title rather than only when
       Save is pressed: it changes where the work is going to end up. */
    const string ro = (!source_.empty() && !sourceWritable())
                      ? "  [read-only]" : "";

    if (source_.empty())
        titleLbl_.set_markup("<i>no file</i>");
    else
        titleLbl_.set_markup("<b>" + Glib::Markup::escape_text(base) + "</b>" +
                             (dirty() ? " *" : "") +
                             Glib::Markup::escape_text(ro));

    titleLbl_.set_tooltip_text(source_);
}

void NodeEditor::updateDirty (void)
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

bool NodeEditor::attached (void) const
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
const char *NodeEditor::applyControlLive (const string &name, double value)
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
const char *NodeEditor::applyValueLive (const string &node, const string &arg,
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

bool NodeEditor::copyFile (const string &from, const string &to)
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

bool NodeEditor::sourceWritable (void) const
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
bool NodeEditor::startWorkingCopy (const string &source)
{
    if (work_.empty())
    {
        /* The name has to be unguessable and the directory has to be the one
           this platform actually uses -- see thUtil::tempFile, which is where
           both of those live now. It used to be built here from TMPDIR with
           "/tmp" as the fallback, which is how Windows ended up looking for a
           directory it does not have and reporting that it could not read a
           file it had never opened. */
        work_ = thUtil::tempFile("thinksynth-edit-");

        if (work_.empty())
            return false;
    }

    return copyFile(source, work_);
}

bool NodeEditor::open (const string &filename, int chan)
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

bool NodeEditor::reload (void)
{
    const string filename = work_;

    /* The constructor already allows for this when it fills the palette; the
       rest of the class did not, and a NULL synth reached parseTree as a NULL
       `this' and a lock on a mutex at address 0x120b8. Nothing here works
       without a synth to parse with -- the graph comes out of a thSynthTree
       and only thSynth makes one. */
    if (synth_ == NULL)
    {
        setStatus("No synth to parse " + source_ + " with");
        return false;
    }

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

    /* Probes recorded in the file, the first time it is read.
     *
     * Only when nothing is armed: a reload happens after every structural edit
     * as well as on open, and re-reading the block then would resurrect a
     * probe that had just been removed. What is in memory is the live answer
     * once there is one; the file is only ever the starting point. */
    if (probes_.empty())
    {
        vector<NodeLayout::ProbeRef> saved;

        if (NodeLayout::readProbes(filename, saved))
            for (size_t i = 0; i < saved.size(); i++)
            {
                if (visuals_.find(saved[i].visual) == visuals_.end())
                {
                    /* The .dsp names a module this build does not have. Worth
                       saying rather than dropping quietly -- the panel simply
                       not appearing reads as the file having lost it. */
                    setStatus("No visual module '" + saved[i].visual +
                              "' for the probe on " + saved[i].node + "." +
                              saved[i].arg);
                    continue;
                }

                if ((int)probes_.size() >= TH_MAX_PROBES)
                    break;

                Probe p;

                p.node = saved[i].node;
                p.arg = saved[i].arg;
                p.visual = saved[i].visual;
                p.module = visuals_[saved[i].visual];
                p.inst = NULL;
                p.slot = -1;

                probes_.push_back(p);
            }
    }

    /* The graph is new, so every panel in it is gone and every tap in the
       engine refers to a tree that no longer exists. Both are rebuilt from the
       probe list, which is keyed on names for exactly this reason. */
    reapplyProbes();

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

void NodeEditor::onArrange (void)
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
bool NodeEditor::flushPending (string &why)
{
    if (pending_.empty() && wires_.empty() && controls_.empty() &&
        !layoutDirty_)
        return true;

    return writeAll(why);
}

bool NodeEditor::writeAll (string &why)
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

void NodeEditor::onSave (void)
{
    if (work_.empty())
        return;

    const int n = (int)pending_.size();
    const int w = (int)wires_.size();
    const int c = (int)controls_.size();

    string why;

    if (!writeAll(why))
    {
        showError(topLevel(), "Could not save.", why);

        setStatus("Not saved: " + why);
        return;
    }

    /* Everything is now in the working copy. Putting it where the user
       expects is a separate step, and the one that can fail for reasons that
       have nothing to do with the edits -- an installed patch, a read-only
       medium, someone else's file.

       Offer Save As rather than an error. The work is safe either way; all
       that is in question is where it lands.

       That offer used to be a question asked here and answered before this
       line finished. A GTK4 chooser is answered later, so the rest of the
       save is handed over as something to do afterwards. */
    if (!sourceWritable() || !copyFile(work_, source_))
    {
        saveAsDialog(sigc::bind(sigc::mem_fun(*this, &NodeEditor::finishSave),
                                n, c, w),
                     "Not saved: " + source_ + " cannot be written. "
                     "Use Save As to put it somewhere else.");
        return;
    }

    finishSave(n, c, w);
}

/* What follows a save that got as far as a file, from either route. */
void NodeEditor::finishSave (int n, int c, int w)
{
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


void NodeEditor::onRevert (void)
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

void NodeEditor::onBoxMoved (int box)
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
void NodeEditor::onSelected (int box)
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
void NodeEditor::onSelectionChanged (int n)
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

vector<string> NodeEditor::takenNames (void) const
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
void NodeEditor::onPaletteAdd (string spelling)
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

/* The new-control form.
 *
 * It used to be a stack of widgets and a `while (dlg.run() == OK)' loop: ask,
 * validate, complain and ask again without losing what had been typed. GTK4
 * has no run(), so the widgets outlive the call that built them -- hence the
 * form struct -- and the loop becomes the dialog simply not closing when the
 * answer will not do.
 *
 * `done' is what to do with a good answer. */
void NodeEditor::askControl (const string &suggested,
                             const sigc::slot<void (ControlForm *)> &done)
{
    Gtk::Window *top = topLevel();

    if (top == NULL)
        return;

    ControlForm *form = new ControlForm;

    form->done = done;
    form->dlg = new Gtk::Dialog("New control", *top, true);

    form->dlg->add_button("Cancel", Gtk::ResponseType::CANCEL);
    form->dlg->add_button("Add", Gtk::ResponseType::OK);
    form->dlg->set_default_response(Gtk::ResponseType::OK);

    Gtk::Grid *grid = manage(new Gtk::Grid);

    grid->set_row_spacing(4);
    grid->set_column_spacing(8);
    grid->set_margin_start(8);
    grid->set_margin_end(8);
    grid->set_margin_top(8);
    grid->set_margin_bottom(8);

    form->name = manage(new Gtk::Entry);
    form->label = manage(new Gtk::Entry);

    form->name->set_text(suggested);
    form->name->set_activates_default(true);
    form->label->set_activates_default(true);
    form->label->set_placeholder_text("optional");

    form->min = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(0, -1000000, 1000000, 0.1, 1), 0.1, 4));
    form->max = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(1, -1000000, 1000000, 0.1, 1), 0.1, 4));
    form->value = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(0.5, -1000000, 1000000, 0.1, 1), 0.1, 4));

    const char *labels[] = { "Name", "Label", "Minimum", "Maximum", "Value" };
    Gtk::Widget *fields[] = { form->name, form->label, form->min, form->max,
                              form->value };

    for (int i = 0; i < 5; i++)
    {
        Gtk::Label *l = manage(new Gtk::Label(labels[i]));

        l->set_xalign(1.0);

        grid->attach(*l, 0, i, 1, 1);
        grid->attach(*fields[i], 1, i, 1, 1);
    }

    Gtk::Label *hint = manage(new Gtk::Label);

    hint->set_markup("<small>The name is what nodes read as "
                     "<tt>@name</tt>.</small>");
    hint->set_xalign(0.0);
    grid->attach(*hint, 0, 5, 2, 1);

    grid->set_hexpand(true);
    form->dlg->get_content_area()->append(*grid);

    form->dlg->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &NodeEditor::onAskControlResponse),
                   form));

    form->dlg->present();
}

void NodeEditor::onAskControlResponse (int response, ControlForm *form)
{
    if (response != Gtk::ResponseType::OK)
    {
        closeDialog(form->dlg);
        delete form;
        return;
    }

    string complaint;

    if (!NodeEdit::validName(form->name->get_text()))
        complaint = "A control name must start with a letter or underscore "
                    "and contain only letters, digits and underscores.";
    else if (!NodeEdit::validLabel(form->label->get_text()))
        complaint = "A label cannot contain a quote -- the .dsp string syntax "
                    "has no way to escape one.";
    else if (form->max->get_value() <= form->min->get_value())
        complaint = "The maximum must be above the minimum.";

    /* Left open, which is what the loop was for: a rejected name can be
       corrected rather than the whole form being thrown away. */
    if (!complaint.empty())
    {
        showWarning(form->dlg, complaint);
        return;
    }

    const sigc::slot<void (ControlForm *)> done = form->done;

    done(form);

    closeDialog(form->dlg);
    delete form;
}


void NodeEditor::onPaletteAddControl (void)
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

    askControl(NodeCatalog::suggestName("ctl", taken),
               sigc::mem_fun(*this, &NodeEditor::addControlFromForm));
}

void NodeEditor::addControlFromForm (ControlForm *form)
{
    const string name = form->name->get_text();
    const string label = form->label->get_text();
    const double value = form->value->get_value();
    const double min = form->min->get_value();
    const double max = form->max->get_value();

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
void NodeEditor::onDeleteNode (void)
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
void NodeEditor::saveAsDialog (const sigc::slot<void ()> &done,
                               const string &ifRefused)
{
    Gtk::Window *top = topLevel();

    if (top == NULL)
        return;   /* not in a window yet; nothing to parent on */

    Gtk::FileChooserDialog *dlg =
        new Gtk::FileChooserDialog(*top, "Save DSP As",
                                   Gtk::FileChooser::Action::SAVE);

    dlg->set_modal(true);
    dlg->set_transient_for(*top);
    dlg->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dlg->add_button("Save", Gtk::ResponseType::OK);

    if (!source_.empty())
    {
        dlg->set_current_name(thUtil::basename(source_.c_str()));

        /* Not the source's own folder as the starting point when it cannot be
           written -- offering the directory that just refused the write is
           not much of a suggestion. */
        if (sourceWritable())
            dlg->set_current_folder(
                Gio::File::create_for_path(thUtil::dirname(source_.c_str())));
    }
    else
        dlg->set_current_name("untitled.dsp");

    dlg->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &NodeEditor::onSaveAsResponse),
                   dlg, done, ifRefused));

    dlg->present();
}

void NodeEditor::onSaveAsResponse (int response, Gtk::FileChooserDialog *dlg,
                                   sigc::slot<void ()> done, string ifRefused)
{
    const string path = response == Gtk::ResponseType::OK
                        ? chosenPath(*dlg) : string();

    closeDialog(dlg);

    if (path.empty())
    {
        if (!ifRefused.empty())
            setStatus(ifRefused);

        return;
    }

    confirmOverwrite(topLevel(), path,
        sigc::bind(sigc::mem_fun(*this, &NodeEditor::saveAsConfirmed),
                   path, done, ifRefused));
}

void NodeEditor::saveAsConfirmed (string path, sigc::slot<void ()> done,
                                  string ifRefused)
{
    if (!copyFile(work_, path))
    {
        showError(topLevel(), "Could not save there.", path);

        if (!ifRefused.empty())
            setStatus(ifRefused);

        return;
    }

    /* Adopting the new path matters: after saving a read-only patch somewhere
       of your own, the next Save should go there without asking again. */
    source_ = path;

    done();
}

void NodeEditor::onSaveAs (void)
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

    saveAsDialog(sigc::mem_fun(*this, &NodeEditor::finishSaveAs), string());
}

void NodeEditor::finishSaveAs (void)
{
    structuralDirty_ = false;

    const int sel = canvas_.selected();

    if (!reload())
        return;

    if (sel >= 0 && sel < (int)graph_.boxes().size())
        canvas_.setSelected(sel);

    setStatus("Saved as " + source_ + ".");
}


void NodeEditor::onNewFile (void)
{
    Gtk::Window *top = topLevel();

    if (top == NULL)
        return;

    Gtk::FileChooserDialog *dlg =
        new Gtk::FileChooserDialog(*top, "New DSP",
                                   Gtk::FileChooser::Action::SAVE);

    dlg->set_modal(true);
    dlg->set_transient_for(*top);
    dlg->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dlg->add_button("Create", Gtk::ResponseType::OK);
    dlg->set_current_name("untitled.dsp");

    if (!source_.empty())
        dlg->set_current_folder(
            Gio::File::create_for_path(thUtil::dirname(source_.c_str())));

    dlg->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &NodeEditor::onNewFileResponse), dlg));

    dlg->present();
}

void NodeEditor::onNewFileResponse (int response, Gtk::FileChooserDialog *dlg)
{
    const string path = response == Gtk::ResponseType::OK
                        ? chosenPath(*dlg) : string();

    closeDialog(dlg);

    if (path.empty())
        return;

    confirmOverwrite(topLevel(), path,
        sigc::bind(sigc::mem_fun(*this, &NodeEditor::createFileAt), path));
}

void NodeEditor::createFileAt (string path)
{
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
        showError(topLevel(), "Could not create the file.", why);
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
void NodeEditor::onParamEdited (int box, string name, double value)
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
void NodeEditor::onConnect (int fromBox, int fromPort, int toBox, int toPort)
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

void NodeEditor::onDisconnect (int edge)
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
void NodeEditor::onControlChanged (int box, double value, bool commit)
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

void NodeEditor::onRefused (string why)
{
    setStatus("Cannot connect: " + why);
}

void NodeEditor::onZoomIn (void)
{
    canvas_.setZoom(canvas_.zoom() * 1.25);
}

void NodeEditor::onZoomOut (void)
{
    canvas_.setZoom(canvas_.zoom() / 1.25);
}

void NodeEditor::onZoomReset (void)
{
    canvas_.setZoom(1.0);
}

void NodeEditor::onZoomFit (void)
{
    canvas_.zoomToFit();
}

/* ------------------------------------------------------------------------
 * Probes. See the Probe struct in NodeEditor.h for what one is made of.
 * ------------------------------------------------------------------------ */

void NodeEditor::scanVisuals (void)
{
    thPluginManager *pm = synth_ ? synth_->getPluginManager() : NULL;

    string root = pm ? pm->pluginPath() : string(PLUGIN_PATH);

    if (root.empty() || root[root.size() - 1] != '/')
        root += '/';

    root += "visual/";

    /* Kept so the menu can say where it looked.
     *
     * "No visual modules installed" on its own is a bad message, and it cost a
     * real debugging session: thPluginManager::resolveRoot tries ./plugins
     * before the directory next to the binary, so running an uninstalled build
     * from the top of the source tree finds the *source* plugins/ -- which,
     * in a tree that once had an autotools build, still holds 62 stale .so
     * files and satisfies the "does this look like a plugin root" test. Every
     * DSP plugin then loads from there too, silently, and visual/ has only a
     * .cpp in it. Naming the directory turns that from a mystery into a
     * sentence. */
    visualRoot_ = root;

    std::error_code ec;

    /* A tree with no visual modules is not an error -- the editor works
       exactly as it did before them, minus the menu entries. */
    if (!std::filesystem::is_directory(root, ec))
        return;

    for (const auto &f : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        thVisual *v = new thVisual(f.path().string());

        if (v->state() != thVisual::LOADED)
        {
            /* thVisual has already said why on stderr. */
            delete v;
            continue;
        }

        /* Keyed by the name the module gives itself rather than by its
           filename. That is what a `# @probe' line records and what the menu
           shows, and a module whose two disagreed would be findable by one
           spelling and not the other. */
        if (visuals_.find(v->name()) != visuals_.end())
        {
            fprintf(stderr, "NodeEditor: two visual modules both called '%s'; "
                    "keeping %s\n", v->name().c_str(),
                    visuals_[v->name()]->path().c_str());
            delete v;
            continue;
        }

        visuals_[v->name()] = v;
    }
}

int NodeEditor::probeForBox (int box) const
{
    if (box < 0 || box >= (int)graph_.boxes().size())
        return -1;

    const NodeGraph::Box &b = graph_.boxes()[box];

    if (!b.isProbe || b.attachedTo < 0)
        return -1;

    const string &host = graph_.boxes()[b.attachedTo].name;

    for (size_t i = 0; i < probes_.size(); i++)
        if (probes_[i].node == host && probes_[i].arg == b.probeArg)
            return (int)i;

    return -1;
}

bool NodeEditor::armProbe (const string &node, const string &arg,
                           const string &visual)
{
    std::map<std::string, thVisual *>::iterator m = visuals_.find(visual);

    if (m == visuals_.end())
    {
        setStatus("No visual module called '" + visual + "'");
        return false;
    }

    /* Already watching this point: retarget rather than stack a second panel
       on the same signal. */
    for (size_t i = 0; i < probes_.size(); i++)
        if (probes_[i].node == node && probes_[i].arg == arg)
        {
            if (probes_[i].visual == visual)
                return true;

            disarmProbe(i);
            break;
        }

    if ((int)probes_.size() >= TH_MAX_PROBES)
    {
        char buf[128];

        snprintf(buf, sizeof(buf), "All %d probes are in use -- remove one "
                 "first", TH_MAX_PROBES);
        setStatus(buf);
        return false;
    }

    Probe p;

    p.node = node;
    p.arg = arg;
    p.visual = visual;
    p.module = m->second;
    p.inst = NULL;
    p.slot = -1;

    probes_.push_back(p);

    /* A probe is written into the file's layout block, so arming one is an
       unsaved change like moving a box. Set here rather than in
       reapplyProbes, which also runs on open -- a patch that came with probes
       in it is not dirty for having them. */
    layoutDirty_ = true;

    reapplyProbes();
    updateDirty();

    setStatus("Watching " + node + "." + arg + " with " + visual +
              (attached() ? "" : " -- nothing is playing this patch"));

    return true;
}

/* Lets go of everything a probe holds outside probes_. Separate from erasing
   it so that disarming several does not rebuild the graph once per probe --
   and, at teardown, does not re-arm the ones not gone yet. */
void NodeEditor::releaseProbe (Probe &p)
{
    if (p.module && p.inst)
        p.module->close(p.inst);

    p.inst = NULL;

    if (synth_ && p.slot >= 0)
        synth_->disarmProbe(p.slot);

    p.slot = -1;
}

void NodeEditor::disarmProbe (size_t index)
{
    if (index >= probes_.size())
        return;

    releaseProbe(probes_[index]);

    probes_.erase(probes_.begin() + index);

    layoutDirty_ = true;

    reapplyProbes();
    updateDirty();
}

void NodeEditor::disarmAllProbes (void)
{
    for (size_t i = 0; i < probes_.size(); i++)
        releaseProbe(probes_[i]);

    probes_.clear();

    reapplyProbes();
}

/* Rebuilds everything a probe owns outside itself: its panel in the graph, its
   instance, and its tap in the engine.
 *
 * Called whenever any of the three could have gone stale, which is after every
 * reload and after every arm or disarm. Doing all of it every time rather than
 * patching up the difference is deliberate -- there are at most eight, none of
 * it is expensive, and the alternative is three kinds of partial update to get
 * wrong. */
void NodeEditor::reapplyProbes (void)
{
    /* Panels first, from the names. Any panel already in the graph belongs to
       the graph that has just been thrown away. */
    for (int b = (int)graph_.boxes().size() - 1; b >= 0; b--)
        if (graph_.boxes()[b].isProbe)
            graph_.removeProbe(b);

    for (size_t i = 0; i < probes_.size(); i++)
    {
        Probe &p = probes_[i];

        int host = -1;

        for (size_t b = 0; b < graph_.boxes().size(); b++)
            if (!graph_.boxes()[b].isProbe && !graph_.boxes()[b].isControl &&
                graph_.boxes()[b].name == p.node)
            {
                /* The io node is one node in the file and two boxes on
                   screen, and only the source half has outputs. Picking by
                   name alone would find whichever came first. */
                bool has = false;

                for (size_t k = 0; k < graph_.boxes()[b].ports.size(); k++)
                    if (!graph_.boxes()[b].ports[k].isInput &&
                        graph_.boxes()[b].ports[k].name == p.arg)
                        has = true;

                if (has)
                {
                    host = (int)b;
                    break;
                }
            }

        if (host < 0)
            continue;   /* the node or the port is gone; see below */

        const double h = NodeGraph::probeHeadHeight() +
                         (p.module ? p.module->preferredHeight() : 24);

        graph_.addProbe(host, p.arg, p.visual, h);
    }

    graph_.layout();

    /* Instances. One per probe, opened at the synth's rate. */
    for (size_t i = 0; i < probes_.size(); i++)
    {
        Probe &p = probes_[i];

        if (p.inst == NULL && p.module)
            p.inst = p.module->open(synth_ ? (unsigned int)
                                    synth_->getSampleRate() : 44100u);
    }

    /* Taps. Re-asked rather than kept, because loading a patch onto the
       channel disarms every probe on it -- the ids a probe holds are only
       meaningful against the tree they were measured on. */
    for (size_t i = 0; i < probes_.size(); i++)
    {
        Probe &p = probes_[i];

        if (!attached())
        {
            p.slot = -1;
            continue;
        }

        if (p.slot >= 0 && synth_->probe(p.slot) != NULL)
            continue;   /* still good */

        string why;

        p.slot = synth_->armProbe(channel_, p.node, p.arg, why);
    }

    /* Four windows at a time, so a drain that has fallen a frame or two behind
       catches up in one read rather than four.

       Compared against the size actually wanted rather than against one
       window: with the old test a buffer sized for a 1024-sample window stayed
       at 4096 after the window grew to 2048, which is not wrong but quietly
       costs twice the reads it should. */
    {
        const size_t want =
            (size_t)(synth_ ? synth_->getWindowlen() : 1024) * 4;

        if (probeDrain_.size() < want)
            probeDrain_.resize(want);
    }

    updateProbeTick();

    canvas_.queue_draw();
}

void NodeEditor::updateProbeTick (void)
{
    /* Only when something can actually change.
     *
     * A probe on a patch nothing is playing has no tap and never will until
     * the channel is loaded, so thirty wake-ups a second would redraw an
     * unchanging panel forever. An editor looking at a .dsp that is not loaded
     * -- which is most of them, most of the time -- should cost what it did
     * before probes existed.
     *
     * An open enlarged window keeps it running regardless of that, because
     * something has to notice when its probe goes and close it, and the tick
     * is that something. It stops on the next pass, once syncEnlarged has
     * emptied the list. */
    const bool want = (attached() && !probes_.empty()) || !enlarged_.empty();

    if (want == probeTick_.connected())
        return;

    if (want)
    {
        /* 30fps. Fast enough to read a meter and slow enough that the 0.8ms
           repaint measured by canvasbench is under 3% of the budget. */
        probeTick_ = Glib::signal_timeout().connect(
            sigc::mem_fun(*this, &NodeEditor::onProbeTick), 33);
    }
    else
        probeTick_.disconnect();
}

bool NodeEditor::onProbeTick (void)
{
    bool anything = false;

    for (size_t i = 0; i < probes_.size(); i++)
    {
        Probe &p = probes_[i];

        if (p.inst == NULL || synth_ == NULL)
            continue;

        /* No tap: ask for one.
         *
         * This is how a probe recovers from the engine dropping its tap, which
         * a patch load on its channel does to every probe on that channel. The
         * first version set slot to -1 here and then skipped anything with a
         * negative slot at the top of this loop, so the comment promising to
         * ask again on the next frame described something that could never
         * happen -- once dropped, a probe was silent for good.
         *
         * The engine's armProbe rather than the editor's: the panel already
         * exists and the slot is the only thing missing, and the editor's
         * would rebuild the graph from inside a timer. */
        if (p.slot < 0)
        {
            if (!attached())
                continue;

            string why;

            p.slot = synth_->armProbe(channel_, p.node, p.arg, why);

            if (p.slot < 0)
                continue;
        }

        thProbe *tap = synth_->probe(p.slot);

        if (tap == NULL)
        {
            /* Dropped between that arm and now. Try again next frame. */
            p.slot = -1;
            continue;
        }

        /* Everything waiting, not one window: the tap publishes a window at a
           time and this runs at a thirtieth of a second, so there are usually
           one or two and occasionally more. A visual module must not care
           where the boundaries fell, which is the property visualcheck
           asserts. */
        for (;;)
        {
            const unsigned int got =
                tap->read(&probeDrain_[0], (unsigned int)probeDrain_.size());

            if (got == 0)
                break;

            p.module->feed(p.inst, &probeDrain_[0], got);
            anything = true;
        }
    }

    /* One redraw for all of them. queue_draw invalidates the whole widget, so
       calling it per probe would only make the same frame more expensive. */
    if (anything || !probes_.empty())
        canvas_.queue_draw();

    /* And the enlarged windows, which also drops the ones whose probe has
       gone. Here rather than in disarmProbe so that a probe removed by a
       reload -- which does not go through disarmProbe -- is caught too. */
    syncEnlarged();

    return true;
}

void NodeEditor::paintProbe (int box, const Cairo::RefPtr<Cairo::Context> &cr,
                             int w, int h)
{
    const int i = probeForBox(box);

    if (i < 0)
        return;

    const Probe &p = probes_[i];

    if (p.module && p.inst)
        p.module->draw(p.inst, cr->cobj(), w, h);
}

/* Right-click: offer to watch this port, or to stop watching it. */
void NodeEditor::onContextRequested (int box, int port, double x, double y)
{
    if (box < 0 || box >= (int)graph_.boxes().size())
        return;

    const NodeGraph::Box &b = graph_.boxes()[box];

    Gtk::Box *menu = Gtk::manage(new Gtk::Box(Gtk::Orientation::VERTICAL, 2));

    menu->set_margin(4);

    /* On a panel: the only thing to offer is removing it. */
    if (b.isProbe)
    {
        Gtk::Button *btn =
            Gtk::manage(new Gtk::Button("Stop watching " + b.probeArg));

        /* By name, resolved when clicked. An index captured while the menu was
           being built would be stale if anything disarmed a probe in between,
           and "stop watching this" removing a different one is exactly the
           sort of bug that gets blamed on the engine. */
        const string node = graph_.boxes()[b.attachedTo].name;
        const string arg = b.probeArg;

        btn->set_has_frame(false);
        btn->signal_clicked().connect([this, node, arg]() {
            ctxPopover_.popdown();

            for (size_t i = 0; i < probes_.size(); i++)
                if (probes_[i].node == node && probes_[i].arg == arg)
                {
                    disarmProbe(i);
                    setStatus("Stopped watching " + node + "." + arg);
                    return;
                }
        });

        menu->append(*btn);
    }
    else if (port >= 0 && port < (int)b.ports.size() &&
             !b.ports[port].isInput)
    {
        const string node = b.name;
        const string arg = b.ports[port].name;

        if (visuals_.empty())
        {
            Gtk::Label *lbl = Gtk::manage(new Gtk::Label(
                "No visual modules in " + visualRoot_));

            lbl->set_margin(6);
            lbl->set_wrap(true);
            lbl->set_max_width_chars(48);
            lbl->set_tooltip_text(
                "Set THINK_PLUGIN_PATH to the plugins directory of the build "
                "you are running, or remove stale .so files from the source "
                "tree -- thinksynth looks in ./plugins before the directory "
                "beside its own binary.");
            menu->append(*lbl);
        }

        for (std::map<std::string, thVisual *>::iterator v = visuals_.begin();
             v != visuals_.end(); ++v)
        {
            const string name = v->first;

            Gtk::Button *btn =
                Gtk::manage(new Gtk::Button("Watch " + arg + " with " + name));

            btn->set_has_frame(false);
            btn->set_tooltip_text(v->second->desc());
            btn->signal_clicked().connect([this, node, arg, name]() {
                ctxPopover_.popdown();
                armProbe(node, arg, name);
            });

            menu->append(*btn);
        }
    }
    else
    {
        /* Not a port and not a panel. Saying so is better than a menu that
           silently does not appear, which reads as a broken right-click. */
        Gtk::Label *lbl = Gtk::manage(
            new Gtk::Label("Right-click an output port to watch it"));

        lbl->set_margin(6);
        menu->append(*lbl);
    }

    ctxPopover_.set_parent(canvas_);
    ctxPopover_.set_child(*menu);
    ctxPopover_.set_has_arrow(false);

    Gdk::Rectangle at((int)x, (int)y, 1, 1);

    ctxPopover_.set_pointing_to(at);
    ctxPopover_.popup();
}

/* ------------------------------------------------------------------------
 * The enlarged view. See the Enlarged struct in NodeEditor.h.
 * ------------------------------------------------------------------------ */

void NodeEditor::onProbeActivated (int box)
{
    const int i = probeForBox(box);

    if (i < 0)
        return;

    const string node = probes_[i].node;
    const string arg = probes_[i].arg;

    /* Already open: raise it rather than opening a second window on the same
       signal, which would be two views of one instance and no way to tell
       them apart. */
    for (size_t e = 0; e < enlarged_.size(); e++)
        if (enlarged_[e].node == node && enlarged_[e].arg == arg)
        {
            enlarged_[e].win->present();
            return;
        }

    Enlarged en;

    en.node = node;
    en.arg = arg;
    en.win = new Gtk::Window();
    en.area = Gtk::manage(new Gtk::DrawingArea());

    en.win->set_title(node + "." + arg + " -- " + probes_[i].visual);

    /* Four times the panel in each direction, which for a spectrogram is the
       difference between a smudge and a picture. Resizable from there. */
    en.win->set_default_size(512, 320);
    en.win->set_child(*en.area);

    en.area->set_draw_func(
        sigc::bind(sigc::mem_fun(*this, &NodeEditor::paintEnlarged),
                   node, arg));

    /* Closing hides; it does not destroy.
     *
     * The same shape as every other secondary window here -- see
     * MainSynthWindow::onSubWindowClose -- and worth stating precisely, since
     * the obvious reason for it turns out not to be the reason.
     *
     * GTK4's default close-request handler calls gtk_window_destroy, and the
     * fear is that this frees a window allocated with `new' and leaves
     * enlarged_ holding a dangling pointer. Measured: it does not. gtkmm's
     * wrapper holds its own reference, so after a default close the C++ object
     * and its child are both still alive and syncEnlarged reaps them safely.
     * There is no use-after-free here to fix.
     *
     * What it does buy: the list, not the window manager, decides when one of
     * these goes -- so a window that has been closed is hidden rather than
     * destroyed, and onProbeActivated can raise it again with present()
     * without relying on GTK tolerating present-after-destroy, which is not
     * something it promises. And Ctrl+W goes through close() rather than
     * hiding directly, so the title bar's button and the key take one path;
     * two ways to close one window that do different things is how one of them
     * ends up untested. */
    {
        Gtk::Window *win = en.win;

        en.win->signal_close_request().connect([win]() -> bool {
            win->set_visible(false);
            return true;
        }, false);

        /* A key controller in the capture phase rather than an application
           accelerator, because these windows are deliberately not added to the
           application and an accelerator registered there would never reach
           them. See MainSynthWindow::addCloseAccel, which says it at length. */
        Glib::RefPtr<Gtk::EventControllerKey> keys =
            Gtk::EventControllerKey::create();

        keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        keys->signal_key_pressed().connect(
            [win](guint keyval, guint, Gdk::ModifierType state) -> bool {
                if ((keyval == GDK_KEY_w || keyval == GDK_KEY_W) &&
                    (state & Gdk::ModifierType::CONTROL_MASK) ==
                        Gdk::ModifierType::CONTROL_MASK)
                {
                    win->close();
                    return true;
                }

                return false;
            }, false);

        en.win->add_controller(keys);
    }

    enlarged_.push_back(en);

    /* The tick has to be running for syncEnlarged to notice this window's
       probe going away. It already is -- a probe had to exist to get here --
       but saying so keeps the invariant in one place. */
    updateProbeTick();

    en.win->present();

    /* The tick may not be running: a probe can be armed on a patch that is not
       playing, and there is nothing to drain. The window still has to be
       drawn once. */
    en.area->queue_draw();
}

void NodeEditor::paintEnlarged (const Cairo::RefPtr<Cairo::Context> &cr,
                                int w, int h, string node, string arg)
{
    /* Looked up every frame. The probe list is rebuilt by every reload and
       compacted by every disarm, so anything cached when the window opened
       would be pointing at a different probe by now -- or at none. */
    for (size_t i = 0; i < probes_.size(); i++)
        if (probes_[i].node == node && probes_[i].arg == arg)
        {
            if (probes_[i].module && probes_[i].inst)
                probes_[i].module->draw(probes_[i].inst, cr->cobj(), w, h);

            return;
        }

    /* The probe has gone but the window has not caught up yet -- syncEnlarged
       closes it on the next tick. Drawing nothing is better than drawing the
       wrong signal for one frame. */
}

void NodeEditor::syncEnlarged (void)
{
    for (size_t e = 0; e < enlarged_.size(); )
    {
        bool alive = false;

        for (size_t i = 0; i < probes_.size() && !alive; i++)
            if (probes_[i].node == enlarged_[e].node &&
                probes_[i].arg == enlarged_[e].arg)
                alive = true;

        /* A window the user closed counts as gone too: GTK hides it rather
           than destroying it, so without this the list would grow a dead
           entry per open and every one of them would be redrawn forever. */
        if (!alive || !enlarged_[e].win->get_visible())
        {
            delete enlarged_[e].win;
            enlarged_.erase(enlarged_.begin() + e);

            /* Nothing to renumber: the draw functions are bound to names. */
            continue;
        }

        enlarged_[e].area->queue_draw();
        e++;
    }

    /* The tick may have just lost its last reason to run: an editor that is
       not attached keeps it going only for the windows, so closing the last
       one has to stop it. Without this the timer ran forever on an editor with
       no probes and no windows, since updateProbeTick was reached from nowhere
       else once the list was empty. */
    updateProbeTick();
}

void NodeEditor::closeAllEnlarged (void)
{
    for (size_t e = 0; e < enlarged_.size(); e++)
        delete enlarged_[e].win;

    enlarged_.clear();
}
