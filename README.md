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
                         │              ┌────────────────────────Welcome to Infinite Canvas────────────────────────┐
 ▪ Welcome to Infinite C…│              │A zoomable outline: boxes inside boxes, as deep as you like.              │
   ▪ Basics              │              │ ┌───────────Basics────────────┐     ┌────────────Structure─────────────┐ │
     · Try it            │              │ │N          new box (inside th│     │Tab        lift a box out of its p│ │
   ▪ Structure           │              │ •Dbl-click  edit text, or the •     •Shift+Tab  nest it into the box ab• │
     · Idea              │              │ │Drag       move   Corner   re│     │F          wrap the selection in a│ │
     · Plan              │              │ • ┌───Try it───┐              •     •┌──Idea──┐  ┌──Plan──┐  ┌──Done──┐• │
     · Done              │              │ │ │S           │              │     ││        │─►│        │─►│        ││ │
   · Navigation          │              • • └────────────┘              •     •└────────┘  └────────┘  └────────┘• •
   ▪ Connections         │              │ │                             │     │                                  │ │
     · From              │              │ └───────•──────•───────•──────┘     └────────•────────•───────•────────┘ │
     · To                │              │                                                                          │
   · Projects            │              │                                                                          │
                         │              │                                                                          │
                         │              │                                                                          │
                         │              │ ┌─────────Navigation──────────┐     ┌─────────Connections──────────┐     │
                         │              │ │Wheel / + -  zoom      WASD /│     │Drag from a • on one box to an│     │
                         │              • •0            fit everything o•     •Click a line to select it, dou•     •
                         │              │ │Enter        zoom to the sele│     │                              │     │
                         │              │ •Sidebar      click a box in t•-----•                              •     │
                         │              │ │                             │     │  ┌──From──┐      ┌───To───┐  │     │
                         │              │ •                             •     •  └────────┘─────►└────────┘  •     │
                         │              │ │                             │     │                              │     │
                         │              │ └───────•──────•───────•──────┘     └───────•───────•──────•───────┘     │
                         │              │                                                                          │
                         │              •                                                                          •
                         │              │                                                                          │
                         │              │                                                                          │
                         │              │ ┌─────────────────────────────Projects─────────────────────────────┐     │
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
- **Connections.** Drag from one of the `•` anchor points on a box to another box. Lines are
  routed orthogonally from edge to edge, can be selected and named, and hit-testing uses
  exactly the drawn route.
- **Resize** a selected box by its corners. Its whole subtree scales with it, and a box can't
  be shrunk below what its children need.
- **Outline sidebar** with the full tree. Click an entry to fly to that box.
- **Projects** auto-save every edit once a project has been saved. Only the boxes that changed
  are rewritten, and each file is written atomically. Right-click opens the project menu:
  new, open, switch, save, save as, revert and close. Quitting with an unsaved project asks first.

## Controls

| Action | Input |
|---|---|
| New box (inside the selected one) | `N` |
| Select / add to selection / area select | Click / `Shift`+click / `Shift`+drag on empty canvas |
| Move | Drag a box |
| Edit text / rename | Double-click body / header, or `E` / `R` |
| Resize | Drag a corner of a selected box |
| Connect | Drag from a `•` anchor (or `Ctrl`+drag a box) onto another box |
| Name a connection | Double-click the line |
| Delete box (with contents) or connection | `X` or `Del` |
| Un-nest / nest | `Tab` / `Shift`+`Tab` |
| Frame the selection | `F` |
| Zoom | Mouse wheel (toward the cursor), `+` `-`, `[` `]` (×2) |
| Pan | Drag empty canvas, `WASD`, `HJKL` or arrow keys |
| Fit everything / zoom to selection | `0` or `Home` / `Enter` |
| Project menu | Right-click |
| Save / new project / quit | `Ctrl+S` / `Ctrl+N` / `Ctrl+Q` |

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
infinite_canvas -n <name>            start a new, unsaved project
infinite_canvas --snapshot <out.txt> [project.canvas] [--size WxH]
                                     render a project (default: the demo) to a text file
```

Projects are stored in `%USERPROFILE%\.infinite_canvas\projects`.

## Project format

A project is a directory with one small JSON file per box, so auto-saving an edit rewrites a
few hundred bytes instead of the whole document:

```text
Name.canvas/
├── config.json      {"format": "1.1", "canvas_name", "viewport", "connections": [...]}
└── nodes/
    └── <uuid>.json  {"uuid", "name", "text", "parentId", "x", "y", "width", "height", ...}
```

Readers accept any `1.x` file and ignore unknown fields. Files from the original `1.0` format
still open. A damaged box file is skipped (and counted) instead of making the whole project
unopenable.

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
