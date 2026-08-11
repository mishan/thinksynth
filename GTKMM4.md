# Scoping a gtkmm-4 port

Re-measured on `gtkmm4-port` @ the tip of `ui-tabs`, with gtkmm 4.22.0,
gtk4 4.22.4, sigc++ 3.6.0, glibmm 2.88, cairomm 1.16.

> **Superseded numbers.** An earlier revision of this document was written
> against `gtkmm3-port` and an autotools build, and reported 1093 errors
> falling to 490. The tree has since gained the CMake build, the node editor
> and the patch-page work, and it no longer has a `configure.ac` at all.
> Everything below is measured against what is here now.

The short version is unchanged and worth repeating: **this is a substantially
bigger job than 2 → 3 was**, and not because there is more of it. The 2 → 3
port was mostly translation — the same widgets with different spellings. Three
parts of 3 → 4 have no translation at all and have to be redesigned: input
handling, dialogs, and menus.

## Measured, not guessed

Pointing CMake at `gtkmm-4.0`, `sigc++-3.0` and `glibmm-2.68` gives **771
errors**. Everything on this branch so far has been spent getting that number
down *without the tree ever leaving gtkmm-3*, because a change that is valid
under both costs nothing today and is one fewer thing to do while nothing
compiles:

| after | errors | what changed |
|---|---|---|
| baseline | 771 | |
| sigc++-3 signal spellings, `std::mutex` | 759 | two headers the earlier sweep missed, and `Glib::Mutex` |
| orientable `Box`, `Scale`, `Paned` | 700 | `HBox`/`VBox`/`HScale`/`HPaned` are gone; the orientable forms are gtkmm-3.0 |
| `Gtk::Table` → `Gtk::Grid` | 660 | Grid replaced it in gtkmm 3.2 |
| label alignment, `Gtk::Stock`, `get_file()` | 594 | `Gtk::Misc` and stock ids are gone; a chooser answers with a `Gio::File` |
| `sigc::bind` without explicit types | 574 | in sigc++-3 that template argument is a *position* |

So a quarter of it went before the switch was thrown, and the tree built,
ran and passed `dspcheck` at every step.

Two things the earlier survey did not know about, both from work that landed
after it was written:

- `gthMidiQueue.h` and `gthRtMidi.h` declare their event signal in sigc++-2's
  template-argument spelling. They arrived with the RtMidi work, after the
  original sweep, written in the style of everything around them.
- **`NodeCanvas` uses `Cairo::FONT_SLANT_NORMAL` and friends.** The earlier
  document said the Cairo drawing carries over unchanged, which is the one
  piece of luck it claimed. It is not quite true: gtkmm-4 pulls in cairomm-1.16,
  where the toy font enums moved into `Cairo::ToyFontFace`. It is five errors
  and a rename, but it is not free and it is not zero.

The finding that **the port reaches into `libthink`** still holds. gtkmm-4
requires sigc++-3, and sigc++-2 and sigc++-3 cannot coexist in one binary —
they share the `sigc` namespace. `thArg` emits a signal, so the engine's
signal types move too, and `lib_major` will need another bump.

## What the remaining 574 are

| Cluster | Errors | Difficulty |
|---|---|---|
| Box packing (`pack_start`, `PACK_*`) | ~145 | mechanical, voluminous |
| Scoped enums (`Orientation`, `Policy`, `Response`, `Message`, `Align`) | ~110 | mechanical |
| Input handling | ~64 | **redesign** |
| `add()` → `set_child()` | ~25 | mechanical |
| Dialogs | ~20 | **redesign** (control flow inverts) |
| Menus | ~15 | **rewrite** |
| `Gtk::Main`, `show_all`, cairomm, odds and ends | ~195 | mechanical, with a lifecycle change hiding in it |

By file, the weight is where the work of the last year went:

```
120  src/gui/MainSynthWindow.cpp      34  src/gui/PatchSelWindow.cpp
 86  src/gui/NodeEditor.cpp           30  src/gui/AboutBox.cpp
 75  src/gui/MidiMap.cpp              28  src/gui/Keyboard.h
 37  src/gui/NodeCanvas.cpp           27  src/gui/Keyboard.cpp
```

### Box packing and the scoped enums — tedious, not hard

`Gtk::Box::pack_start(child, PACK_SHRINK)` becomes `append(child)` with
`set_hexpand()`/`set_vexpand()` on the child, which is the same move `Table` →
`Grid` already made: the layout flags stop travelling with the call and become
properties of the widget. `pack_end` has no direct equivalent — a box packs in
one direction now, so anything that was packed from the far end has to be
appended in the right order or given `set_halign(END)` and an expanding filler.
That is the one part of this cluster that is a decision rather than a
transcription, and the node editor's toolbar is where it bites.

The enums are pure transcription: `Gtk::ORIENTATION_VERTICAL` becomes
`Gtk::Orientation::VERTICAL`, `Gtk::POLICY_AUTOMATIC` becomes
`Gtk::PolicyType::AUTOMATIC`, and so on. gtkmm-3's unscoped enumerators and
gtkmm-4's scoped ones share no spelling, so none of this could be done early.

`Gtk::Container` is gone with them, so `add()` becomes `set_child()` and
`show_all()`/`show_all_children()` disappear, widgets being visible by default.

### Input handling — a redesign, and it lands on Keyboard and NodeCanvas

GTK4 removed every `on_*_event` vfunc: `on_button_press_event`,
`on_button_release_event`, `on_key_press_event`, `on_key_release_event`,
`on_motion_notify_event`, `on_scroll_event`, `on_focus_in_event`,
`on_focus_out_event`, along with the `GdkEventButton`/`GdkEventKey`/
`GdkEventFocus` structs.

Input arrives through **event controllers** attached to a widget:
`Gtk::GestureClick`, `Gtk::EventControllerKey`, `Gtk::EventControllerMotion`,
`Gtk::EventControllerScroll`, `Gtk::EventControllerFocus`. That is a different
model, not a different spelling — controllers have their own propagation phases
and claim/deny semantics, and a widget no longer decides by returning true or
false.

`Keyboard.cpp` is essentially all input handling, and `NodeCanvas.cpp` is now
too — dragging boxes, rubber-band selection, dragging wires between ports and
dragging the little inline control sliders are all button and motion handling.
Between them this is the bulk of the risk. Specifics that will need care:

- `Keyboard::get_coord()` asks the pointer where it is
  (`Gdk::Window::get_device_position`) rather than using coordinates from the
  event. Controllers hand coordinates to the callback, which is better, but the
  function is called from places that have no event to hand it.
- Key auto-repeat behaviour is what made the transpose bug visible. Controller
  key handling reports repeats differently, so that path wants re-testing
  rather than assuming it still holds.
- `NodeCanvas` distinguishes a click from a drag by distance, and decides what
  a press meant by what is under it. That logic is coordinate arithmetic and
  carries over; what changes is where the coordinates come from and which
  controller claims the sequence.

`DrawingArea` also changes: `on_draw` is replaced by `set_draw_func()`, and the
widget is told its size rather than reading its allocation. Both `Keyboard` and
`NodeCanvas` are `DrawingArea`s. The Cairo drawing itself carries over apart
from the toy font enums noted above.

`Gtk::StyleContext::render_focus` is deprecated in GTK4; focus drawing wants
rethinking, probably as CSS.

### Menus — rewritten a second time

`Gtk::MenuBar`, `Gtk::Menu` and `Gtk::MenuItem` are all removed. GTK4 menus are
a `Gio::Menu` *model* plus `Gtk::PopoverMenuBar`, with behaviour attached as
`Gio::SimpleAction` objects rather than callbacks on items.

This is genuinely better — actions get names, can be enabled and disabled by
name, and accelerators bind to actions rather than widgets. But it is a second
full rewrite of code that was already rewritten once for gtkmm-3. The menu is
smaller than it was: dropping the Node View item took one of the five entries
and the only one whose sensitivity depended on anything.

### Dialogs — control flow inverts

`Gtk::Dialog::run()` is **gone**, and with it the ability to write:

```cpp
if (fileSel.run() == Gtk::RESPONSE_OK) {
    /* use the result here */
}
```

GTK4 dialogs are asynchronous: show the dialog, connect `signal_response()`,
and continue in the callback. Every file chooser and every `MessageDialog`
becomes a continuation. The call sites are `PatchSelWindow` (`BrowsePatch`,
`SavePatch`), `MainSynthWindow` (`onBrowseButton`, `onSavePatchAs`,
`onPatchLoadError`, `onDspEntryActivate`'s error) and `NodeEditor`
(`saveAsDialog`, `onNewFile`, `askControl`, and the error dialogs). About 20
errors, but each one restructures a function, and two of them —
`NodeEditor::saveAsDialog` and `askControl` — currently return a bool that
their callers branch on, so the inversion propagates one level up.

The dialogs are also modal-by-`run()` today. Made asynchronous they are not
modal unless told to be, and `MainSynthWindow::doSavePatch` already exists
because a save emits `signal_patches_changed` and rebuilds the page the button
was on. That interaction wants checking rather than assuming.

### Odds and ends

`Gtk::Main` no longer exists: the application becomes `Gtk::Application` with an
`on_activate`, which restructures `main.cpp` around a different lifecycle. That
is 6 errors and misleadingly small — the shutdown ordering that
`ui-tabs` just fixed (the window is deleted before the synth, and the
preferences are written in between) has to survive the move, and
`Gtk::Application` wants to own the window.

`Gtk::Image` prefers `Gdk::Texture` over `Gdk::Pixbuf`. `Gdk::Screen` is gone,
so `MainSynthWindow::applyPrefs`'s check that a saved window size still fits on
the screen becomes a `Gdk::Monitor` query. Assorted renames follow the enums.

## Suggested order

1. ~~**The free layer.**~~ Done, on this branch: 771 → 574, all of it valid
   under gtkmm-3 and verified there.
2. **The mechanical layer** — box packing, scoped enums, `set_child`,
   `show_all`, cairomm. Big error reduction, low risk, no design decisions.
   Nothing builds from here until step 6 is finished.
3. **`Gtk::Application`** — restructure `main.cpp`. Everything else depends on
   the lifecycle being right.
4. **Menus to `Gio::Menu` + actions.** Self-contained in `MainSynthWindow`.
5. **Dialogs to async.** Self-contained per call site, but each one is a
   rewrite of the surrounding function.
6. **Keyboard and NodeCanvas input to event controllers.** Last, because it is
   the riskiest and the hardest to verify.

## Is it worth doing?

The honest answer has not changed, and it is worth keeping here even though
the work is now under way.

gtkmm-3.24 is stable and maintained, and the tree sits on it cleanly. The
reasons to go to 4 are real but not pressing: GTK3 will eventually stop getting
attention, GTK4 has better input handling and HiDPI, and the menu model is
nicer than what we have. None of that changes what the synth *does*.

Against that: the three redesign areas are exactly the parts of the GUI that
are hardest to verify without sitting in front of it. The transpose bug and the
stray-verticals bug both came from you playing with it, not from anything that
could be checked headlessly — and an input-handling rewrite is a large surface
for that class of bug. Screenshots under Xvfb catch layout; they do not catch
a gesture that claims a sequence it should have let through.

The earlier revision of this section pointed at three higher-value pieces of
work instead. Two of them have since been done and it is worth saying so
rather than leaving the advice standing.

The node editor exists and is a tab on the patch page. And the plugin API
change that `REVIVAL.md` called "the blocker" is in: `regArg()` takes an
`ArgDir`, every in-tree plugin declares one, and `argIsPort()` is what keeps
the editor from offering a delay ring for wiring. It did not need the version
bump that was proposed with it -- `ARG_IN` as the default means a plugin built
against the old header still loads -- so `MODULE_IFACE_VER` is still 4. The
rest of that proposal, a description and a range and a default per arg, is not
done, and it is what would improve the parameter panel rather than the editor.

The Tier 2 lifetime bugs are mostly gone too, though not all by being fixed:
`gthALSAAudio` and `gthALSAMidi` went with the move to RtAudio and RtMidi and
took four of the six items with them, and `~KeyboardWindow`'s double free and
`MidiMap`'s uninitialised members were fixed outright. Two remain, and both
are worth naming here because this port touches one of them:

- `ArgTable` still binds a raw `thArg *` into its slider callbacks, and a raw
  slider pointer into the arg's own signal. The patch bar's amplitude slider
  deliberately does neither -- it goes through the channel number and drops
  its subscriptions in `populate()` -- so the pattern to copy is already in
  the tree, next door.
- `gthPatchManager::newPatch` indexes `patches_[chan]` with no bounds check.
  Nothing reaches it with a bad channel now that the notebook always holds
  sixteen pages, so it is latent rather than live, but it is one line.
