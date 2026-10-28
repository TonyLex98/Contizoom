# Contizoom: Infinite Canvas for the terminal

A keyboard-and-mouse driven **zoomable outliner** that runs in the Windows console.
You write notes in boxes, put boxes inside boxes as deep as you like, connect them
with arrows, and zoom out to see the whole map or zoom into one box until its
contents fill the screen.

Written in modern C++17 with no third-party dependencies: the JSON reader/writer,
UTF-8 handling, layout engine and test harness are all part of the project.

```text
 Infinite Canvas  │  Welcome  │  zoom 48%  │  12 boxes
 OUTLINE                 │
                         │               ┌───────────────────────Welcome to Infinite Canvas───────────────────────┐
 ▪ Welcome to Infinite C…│               │A zoomable outline: boxes inside boxes, as deep as you like.            │
   ▪ Basics              │               │ ┌───────────Basics───────────┐     ┌────────────Structure─────────────┐│
     · Try it            │               │ │N          new box (inside t│     │Tab        lift a box out of its p││
   ▪ Structure           │               │ │Dbl-click  edit text; on the│     │Shift+Tab  nest it into the box ab││
     · Idea              │               │ │Drag       move   Corner   r│     │F          wrap the selection in a││
     · Plan              │               │ │ ┌──Try it───┐              │     │                                  ││
     · Done              │               │ │ │S          │              │     │┌──Idea──┐  ┌──Plan──┐  ┌─Done──┐ ││
   · Navigation          │               │ │ └───────────┘              │     │└────────┘─►└────────┘─►└───────┘ ││
   ▪ Connections         │               │ │                            │     │                                  ││
     · From              │               │ └────────────────────────────┘     └──────────────────────────────────┘│
     · To                │               │                                                                        │
   · Projects            │               │                                                                        │
                         │               │                                                                        │
                         │               │                                                                        │
                         │               │ ┌─────────Navigation─────────┐     ┌─────────Connections──────────┐    │
                         │               │ │Wheel / + -  zoom      WASD │     │Hover a box, drag from one of │    │
                         │               │ │0            fit everything │     │Click a line: T style, V rever│    │
                         │               │ │Enter        zoom to the sel│     │                              │    │
                         │               │ │Sidebar      click a box in │-----│                              │    │
                         │               │ │                            │     │  ┌──From──┐      ┌───To───┐  │    │
                         │               │ │                            │     │  └────────┘─────►└────────┘  │    │
                         │               │ │                            │     │                              │    │
                         │               │ └────────────────────────────┘     └──────────────────────────────┘    │
                         │               │                                                                        │
                         │               │                                                                        │
                         │               │                                                                        │
                         │               │                                                                        │
                         │               │ ┌────────────────────────────Projects─────────────────────────────┐    │
```

<sub>The screenshot above is real program output: `infinite_canvas --snapshot out.txt` renders a
project headlessly into text. The renderer is platform independent, so CI produces the same
picture on Linux.</sub>

## Features

- **Fractal zoom.** Children are laid out proportionally inside their parent's on-screen box.
  As you zoom out, boxes degrade gracefully: first the full box with name and text, then a
  compact box, then a single-cell marker. Several markers that land on one cell merge into a
  cluster. A box with no room left inside stands in for its whole subtree.
- **Editing in place.** Double-click a box to edit its text, or its header to rename it.
  Multi-line text and full Unicode input (Cyrillic, symbols, emoji) are supported, and cursors
  count characters, not bytes.
- **Structure.** Use Tab to lift a box out of its parent and Shift+Tab to nest it into the
  previous sibling. Press F to wrap a selection in a frame. When a box changes parent, it keeps
  its position on screen, and the model refuses moves that would create a cycle.
- **Connections.** Hover a box and drag from one of its `•` anchor points to another box. A live
  preview follows the mouse and snaps to the target's anchors. Lines are routed orthogonally
  from edge to edge and can be selected, named, restyled (arrow, line, dashed, dotted) and
  reversed. Linking the same pair twice selects the existing link. When the endpoints collapse
  into their ancestors as you zoom out, the link is drawn, dimmed, between the visible ancestors.
- **Resize** a selected box by its corners. Its whole subtree scales with it, and a box can't
  be shrunk below what its children need.
- **Outline sidebar** with the full tree. Click an entry to fly to that box.
- **Five languages**: English, Русский, 中文, Français, Español. The language follows the system
  and can be switched at runtime from the menu or with `--lang`. Chinese text is laid out in
  double-width console cells.
- **Undo / redo** for every edit (`Ctrl+Z` / `Ctrl+Y`). A drag or resize is one step.
- **Context menus.** Right-click a box, a line or empty canvas.
- **Safe storage.** A project is saved automatically from its first edit on, so there is no
  "unsaved" state to lose. Auto-save writes only what changed: one file per edited box, and
  `config.json` for the camera and links. Every file is written atomically. A lock file warns
  when the same project is open in two windows. Projects can be copied ("Save a copy as") and
  exported to a Markdown outline.

## Controls

| Action | Input |
|---|---|
| New box (inside the selected one / at a spot) | `N` / double-click empty canvas, or right-click → New box here |
| Select / add to selection / area select | Click / `Shift`+click / `Shift`+drag on empty canvas |
| Move | Drag a box |
| Edit text / rename | Double-click body / header, or `E` / `F2` |
| Resize | Drag a corner of a selected box |
| Connect | Hover a box, drag from a `•` anchor (or `Ctrl`+drag the box) onto another box |
| Connection: style / reverse / rename | Select the line, then `T` / `V` / `F2` (or double-click it) |
| Delete box (with contents) or connection | `X` or `Del` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Y` |
| Un-nest / nest | `Tab` / `Shift`+`Tab` |
| Frame the selection | `F` |
| Zoom | Mouse wheel (toward the cursor), `+` `-`, `[` `]` (×2) |
| Pan | Drag empty canvas, `WASD`, `HJKL` or arrow keys |
| Fit everything / zoom to selection | `0` or `Home` / `Enter` |
| Context menu (box, line or project) | Right-click |
| Save now / new project / quit | `Ctrl+S` / `Ctrl+N` / `Ctrl+Q` |

While editing: `Enter` inserts a new line (it confirms in a name), and `Ctrl+Enter` or a click
outside saves. `Esc` cancels.

## Building

Requirements: CMake 3.16+ and a C++17 compiler. The interactive editor needs Windows
(it uses the Win32 console API). The core library, the tests and `--snapshot` build anywhere.

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

With MSYS2/MinGW:

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

## Usage

```text
infinite_canvas                      open the welcome project (created on first launch)
infinite_canvas <project.canvas>     open a project
infinite_canvas -n <name>            start a new project
infinite_canvas --snapshot <out.txt> [project.canvas] [--size WxH]
                                     render a project (default: the demo) to a text file
infinite_canvas --lang en|ru|zh|fr|es ...   interface language
```

Projects are stored in `%USERPROFILE%\.infinite_canvas\projects`. The chosen language is kept in
`%USERPROFILE%\.infinite_canvas\settings.json`.

## Languages

All interface text lives in one catalog, [`include/i18n/messages.def`](include/i18n/messages.def).
Each entry holds a key and its translations side by side:

```cpp
IC_MSG(StatusSaved, "Saved to {0}", "Сохранено: {0}", "已保存到 {0}", "Enregistré dans {0}", "Guardado en {0}")
```

The same X-macro list generates the `Msg` enum and the lookup table, so a key can't exist
without its text, and an entry with a missing translation doesn't compile. Code asks for
`tr(Msg::StatusSaved, {path})` and never contains user-facing literals. A test checks that every
translation keeps the same `{n}` placeholders as the English text. To add a language, add an
enum value and a native name, then give every entry one more argument.

## Project format

A project is a directory with one small JSON file per box, so auto-saving an edit rewrites a
few hundred bytes instead of the whole document:

```text
Name.canvas/
├── config.json      {"format": "1.1", "canvas_name", "modified_at", "node_count",
│                     "viewport", "connections": [...]}
├── .lock            "<pid> <session>" while the project is open
└── nodes/
    └── <uuid>.json  {"uuid", "name", "text", "parentId", "x", "y", "width", "height", ...}
```

- **What gets written.** Editing a box rewrites its file and `config.json`, deleting a box
  removes its file, and moving the camera rewrites `config.json` only. An undo or redo rewrites
  everything.
- **Atomic writes.** Every file is written to a `.tmp` file and then renamed. A crash can leave
  an old file, but never a half-written one.
- **Compatibility.** Readers accept any `1.x` file and ignore unknown fields, and files from the
  original `1.0` format still open. A damaged box file is skipped (and counted) instead of
  making the whole project unopenable.
- **Export.** "Export as Markdown" writes `Name.md` next to the project: headings for boxes,
  paragraphs for their text, and a list of the connections.

## Architecture

```text
            ┌──────────────┐  events   ┌──────────────┐  edits   ┌────────────┐
 console ──►│ ConsoleInput │──────────►│ InputHandler │─────────►│   Canvas   │ model
            └──────────────┘           └──────┬───────┘          └─────┬──────┘
                                              │ UiState (selection,    │
                                              ▼  hover, editors)       │ changes
            ┌───────────────┐ CellBuffer┌─────────────────┐            ▼
 console ◄──│ ConsoleOutput │◄──────────│ ConsoleRenderer │   ┌────────────────┐
            └───────────────┘           └─────────────────┘   │ ProjectManager │──► JsonStorage
                                                              └────────────────┘
```

- `model/`: `Canvas`, `Node` and `Connection`. They own the hierarchy invariants (no cycles,
  consistent depth) and batch change notifications for auto-save. The model has no UI state.
- `view/`: the layout and rendering into an off-screen `CellBuffer`, plus the screen geometry
  (anchors, corners, connection routes) shared with hit-testing.
- `input/`: `InputHandler`, which turns screen-space events into model edits, as a gesture
  state machine (click, drag, pan, resize, connect, area select).
- `storage/`: `JsonStorage` on top of the in-house `util/json` parser.
- `platform/`: the only Win32 code: raw console input, frame output and modal popups.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for details.

## Tests

`ctest` runs the unit tests in `tests/`: UTF-8 and JSON edge cases, hierarchy operations and
damaged-data repair, storage round-trips (including legacy and corrupted projects), rendering,
and full input scenarios driven through the real renderer layout (click, drag, rename, connect,
zoom). CI builds with MSVC and GCC with warnings as errors, and runs the tests under
AddressSanitizer and UndefinedBehaviorSanitizer.
