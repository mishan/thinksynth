# Scoping a gtkmm-4 port

Written against `gtkmm4-scope` @ the tip of `gtkmm3-port`, measured with
gtkmm 4.22.0, gtk4 4.22.4, sigc++ 3.6.0, glibmm 2.88.

The short version: **this is a substantially bigger job than 2 → 3 was**, and
not because there is more of it. The 2 → 3 port was mostly translation — the
same widgets with different spellings. Three parts of 3 → 4 have no translation
at all and have to be redesigned: input handling, dialogs, and menus.

## Measured, not guessed

Switching `configure.ac` to `gtkmm-4.0` + `sigc++-3.0` and building gives
**1093 errors**. That number is misleading, and worth taking apart before
drawing any conclusions from it:

| after | errors | what changed |
|---|---|---|
| baseline | 1093 | |
| sigc++-3 signal spellings | 750 | five typedefs |
| `PREFIX` macro renamed | 490 | one line of `configure.ac` |

Two thirds of the apparent work was two mechanical fixes.

**Both have been applied on this branch**, because both are valid under
sigc++-2 and gtkmm-3 as well, so they cost nothing today:

- `sigc::signal1<void, thArg*>` → `sigc::signal<void(thArg*)>` and friends.
  sigc++-2.10 onwards accepts the function-style spelling, so this builds
  either way. Five typedefs, in `thArg.h`, `gthSignal.h`, `gthPatchfile.h`,
  `gthALSAAudio.h`, `gthALSAMidi.h` and `Keyboard.h`.
- `configure.ac` defined a bare `PREFIX` macro. gtkmm-4 has
  `Gtk::StringFilter::MatchMode::PREFIX`, so the macro rewrote the enumerator,
  which broke the enum, which broke the class, which took 250 errors' worth of
  `stringfilter.h` and `config.h` down with it. Nothing in the tree ever
  referenced the macro. Renamed to `TH_PREFIX`.

Verified after both: gtkmm-3 build still clean, `dspcheck` still 81/92, GUI
still starts.

The interesting finding from the first row is that **the port reaches into
`libthink`**. `thArg.h` alone accounted for 258 of the original errors, because
gtkmm-4 requires sigc++-3 and the two cannot coexist in one binary — they share
the `sigc` namespace. So this is not a GUI-only change: the engine's signal
types move too, and `lib_major` will need another bump.

## What the remaining 490 actually are

| Cluster | Errors | Difficulty |
|---|---|---|
| Containers and layout | ~200 | mechanical, voluminous |
| Input handling | ~57 | **redesign** |
| Menus | ~40 | **rewrite** |
| Dialogs | ~20 | **redesign** (control flow inverts) |
| Odds and ends | ~170 | mechanical |

### Containers and layout — tedious, not hard

`Gtk::Container` is gone. `add()` becomes `set_child()`, `pack_start()` becomes
`append()`, `Gtk::HBox`/`VBox`/`HScale` become `Box`/`Scale` with an
orientation, and `show_all()`/`show_all_children()` disappear because widgets
are visible by default.

`Gtk::Table` is **removed outright** in GTK4 (it was merely deprecated in 3),
so every `attach()` moves to `Gtk::Grid`. That is not a rename: `Table::attach`
took `SHRINK`/`FILL`/`EXPAND` flags per child, and `Grid::attach` takes a span
and expects `set_hexpand()`/`set_halign()` on the child instead. `ArgTable`,
`MidiMap` and `MainSynthWindow` all use it. Roughly 200 errors, all
individually obvious.

### Input handling — a redesign, and it lands on Keyboard

GTK4 removed every `on_*_event` vfunc. `on_button_press_event`,
`on_button_release_event`, `on_key_press_event`, `on_key_release_event`,
`on_motion_notify_event`, `on_focus_in_event`, `on_focus_out_event` — all gone,
along with the `GdkEventButton`/`GdkEventKey`/`GdkEventFocus` structs.

Input arrives through **event controllers** attached to a widget:
`Gtk::GestureClick`, `Gtk::EventControllerKey`, `Gtk::EventControllerMotion`,
`Gtk::EventControllerFocus`. That is a different model, not a different
spelling — controllers have their own propagation phases and claim/deny
semantics, and a widget no longer decides by returning true or false.

`Keyboard.cpp` is essentially all input handling, so this is the bulk of the
risk. Two specifics that will need care:

- `get_coord()` currently asks the pointer where it is
  (`Gdk::Window::get_device_position`) rather than using coordinates from the
  event. Controllers hand coordinates to the callback, which is better, but
  the function is called from places that have no event to hand it.
- Key auto-repeat behaviour is what made the transpose bug visible. Controller
  key handling reports repeats differently, so that path wants re-testing
  rather than assuming it still holds.

`DrawingArea` also changes: `on_draw` is replaced by `set_draw_func()`, and the
widget is told its size rather than reading its allocation. The Cairo drawing
itself carries over unchanged, which is the one piece of luck here — the 2 → 3
work is not wasted.

`Gtk::StyleContext::render_focus` is deprecated in GTK4; focus drawing wants
rethinking, probably as CSS.

### Menus — rewritten a second time

`Gtk::MenuBar`, `Gtk::Menu` and `Gtk::MenuItem` are all removed. GTK4 menus are
a `Gio::Menu` *model* plus `Gtk::PopoverMenuBar`, with behaviour attached as
`Gio::SimpleAction` objects rather than callbacks on items.

This is genuinely better — actions get names, can be enabled and disabled by
name, and accelerators bind to actions rather than widgets, which is exactly
what `toggleConnects()` wants. But it is a second full rewrite of code that was
already rewritten once for gtkmm-3, and the JACK submenu's runtime sensitivity
toggling has to move to action state.

### Dialogs — control flow inverts

`Gtk::Dialog::run()` is **gone**, and with it the ability to write:

```cpp
if (fileSel.run() == Gtk::RESPONSE_OK) {
    /* use the result here */
}
```

GTK4 dialogs are asynchronous: show the dialog, connect `signal_response()`,
and continue in the callback. Every use of a file chooser and every
`MessageDialog` becomes a continuation. That affects `PatchSelWindow`
(`BrowsePatch`, `SavePatch`), `MainSynthWindow` (`onBrowseButton`,
`onPatchLoadError`), and it is invasive in a way the error count does not
convey — about 20 errors, but each one restructures a function.

`Gtk::Stock` is also gone, so those button labels become plain strings, which
they should have been already.

### Odds and ends

`Gtk::Main` no longer exists: the application becomes `Gtk::Application` with
an `on_activate`, which restructures `main.cpp` around a different lifecycle
(6 errors, misleadingly small). `Glib::Mutex` is gone in glibmm-2.68 →
`std::mutex`, which `Keyboard` uses. `Gtk::Image` prefers `Gdk::Texture` over
`Gdk::Pixbuf`. Assorted renames: `set_alignment`, `POLICY_AUTOMATIC`,
`set_lines`.

## Suggested order

Each step should build and run before the next.

1. **libthink to sigc++-3** — done, and it cost nothing.
2. **The mechanical layer** — containers, `Table` → `Grid`, `show_all`,
   `Glib::Mutex`. Big error reduction, low risk, no design decisions.
3. **`Gtk::Application`** — restructure `main.cpp`. Everything else depends on
   the lifecycle being right.
4. **Menus to `Gio::Menu` + actions.** Self-contained in `MainSynthWindow`.
5. **Dialogs to async.** Self-contained per call site, but each one is a
   rewrite of the surrounding function.
6. **Keyboard input to event controllers.** Last, because it is the riskiest
   and the hardest to verify, and because leaving it late means everything
   else is already known-good.

## Is it worth doing?

Not urgently, and I would not start it as the next piece of work.

gtkmm-3.24 is stable and maintained, and the tree now sits on it cleanly. The
reasons to go to 4 are real but not pressing: GTK3 will eventually stop getting
attention, GTK4 has better input handling and HiDPI, and the menu model is
genuinely nicer than what we have. None of that changes what the synth *does*.

Against that: the three redesign areas are exactly the parts of the GUI that
are hardest to verify without sitting in front of it. The transpose bug and the
stray-verticals bug both came from you playing with it, not from anything I
could check headlessly — and an input-handling rewrite is a large surface for
that class of bug.

If the goal is a better instrument rather than a more modern toolkit, the
higher-value work is elsewhere: the DSP node editor this whole revival was
aimed at, the plugin API v5 that would give the editor real port metadata, or
the remaining Tier 2 GUI lifetime bugs in `REVIVAL.md`.

If you do want it, the order above keeps it in landable pieces, and the two
free fixes are already in.
