# Architecture

## Layers

| Layer | Directory | Depends on | Platform |
|---|---|---|---|
| Utilities: UTF-8, JSON, XML, clock, paths | `util/` | std only | any |
| Document model | `model/` | util | any |
| Persistence | `storage/` | model, util | any |
| Layout and rendering into a cell buffer | `view/` | model | any |
| Input interpretation | `input/` | model, view geometry | any |
| Projects, demo, app loop | `app/` | everything above | `App` needs Windows |
| Console I/O, popups | `platform/` | view (CellBuffer), input (events) | Windows |

Everything except `platform/` and `App` builds and runs its tests on any OS. The Win32 API is
confined to three small files.

## Model

`Canvas` holds a forest of `Node`s and a set of `Connection`s.

- **Coordinates.** A node's `rect` is relative to its parent's top-left corner, and roots use
  world coordinates. `computeWorldRect()` sums the offsets.
- **Invariants.** `parentId`, `childrenUuids` and `depth` are always consistent, and there
  are no cycles. Every mutation that changes the hierarchy (`reparent`, `promoteNode`,
  `demoteNode`, `createFrame`, `removeSubtree`) maintains them. `reparent()` refuses to move
  a node into its own subtree and keeps the node's world position, so nothing jumps on screen.
- **Repair.** `rebuildHierarchy()` recomputes derived data after loading. It detaches nodes
  whose parent is missing and cuts parent cycles, which can only come from damaged files.
- **Change tracking.** Mutations mark nodes (node data changed) or the canvas (structure,
  viewport or connections changed). `processPendingChanges()` reports them through one
  callback once edits have been quiet for 500 ms, and `flushChanges()` reports them
  immediately.

UI state (selection, hover, the in-place editors, the rubber band) is kept in `UiState`,
outside the model. The model stays serializable and testable, and the renderer derives how
each box is highlighted from `UiState`.

## Rendering

`ConsoleRenderer::render(canvas, ui, hud)` composes a whole frame into a `CellBuffer`.
`ConsoleOutput` presents it with a single `WriteConsoleOutputW` call, so frames never flicker.

1. **Screen rects.** A root is placed by the viewport transform
   (`(world - center) * scale + areaCenter`). A child is placed proportionally inside its
   parent's screen rect and clamped inside the parent's border, so rounding errors can't push
   a child onto its parent's edge, even across many levels.
2. **Level of detail.** Nodes are visited parents first. A node is a full box (≥ 4×2 cells),
   a compact box (≥ 2×2) or a marker. Its children are only considered if the node is a full
   box with interior room. Markers compete for cells, and when several land on one cell they
   become a cluster `*`.
3. **Drawing order.** Boxes (with opaque interiors) come first, then connections, then names
   (parents over children), connection labels, overlays (rubber band, editors), and finally
   the sidebar and status rows.
4. **Hit-test data.** The renderer exports the screen rect of every node it actually drew.
   `InputHandler` hit-tests against these rects, so only what is visible can be clicked.

`view/geometry.h` holds the cell-level geometry used by both sides: integer cell boxes, anchor
points, resize corners and orthogonal connection routes. A click on a connection is tested
against the route that was drawn.

## Input

`InputHandler` is a small gesture state machine:

```text
None ──press on box──► PendingClick ──moved > 2 cells──► DragNodes
  │                         └──released──► (edit if the box was already selected)
  ├──press on corner──► Resize
  ├──press on anchor──► Connect ──release on box──► connection created
  ├──Shift+press──────► AreaSelect
  └──press on empty───► Pan
```

Movement is converted from screen cells to model units using the parent's last drawn size
(`unitsPerCell`). Because of the fractal layout, one cell is worth a different number of units
at every level. When a drag includes both a parent and its child, only the top-level nodes
move, so the child doesn't move twice.

Letter shortcuts use virtual key codes, so they work on any keyboard layout. Typed text uses
the character, with UTF-16 surrogate pairs combined into code points in `ConsoleInput`.

Every edit goes through `InputHandler::edit()`, which takes a snapshot before the change and
records it in `History` if the document actually changed. A drag or resize takes its snapshot
when the gesture starts and records it when the gesture ends, so each one is a single undo
step. Snapshots are the compact JSON of the content (see `storage/document_json`). Documents
are small, so storing whole snapshots is cheap, and it makes every action undoable exactly,
with no inverse operation for each command.

Commands are also exposed as `InputHandler::perform(Action)`, which the context menus use, so
keys and menus can never behave differently.

## Internationalization

- **Catalog.** `i18n/messages.def` is an X-macro list,
  `IC_MSG(Key, en, ru, zh, fr, es)`. `i18n.h` expands it into `enum class Msg`, and `i18n.cpp`
  expands it into a `[Msg][Language]` table. Keys and texts can't drift apart, and a missing
  translation is a compile error (wrong macro arity).
- **API.** `tr(Msg)` returns UTF-8 text in the current language, and `tr(Msg, {args})` fills
  `{0}`, `{1}`. Names of model enums (connection styles) are mapped in `view/labels.h`, so
  neither the model nor i18n depends on the other.
- **Choice of language.** `--lang`, then `settings.json`, then the system UI language
  (`GetUserDefaultUILanguage` on Windows, `LC_ALL`/`LC_MESSAGES`/`LANG` elsewhere). Switching
  from the menu takes effect on the next frame and is saved.
- **Width, not length.** Layout measures text in console cells with `utf8::width`. CJK
  characters take two cells: `CellBuffer` stores them as a Lead/Trail pair (overwriting either
  half clears the other), and `ConsoleOutput` marks them with
  `COMMON_LVB_LEADING_BYTE`/`TRAILING_BYTE`. Editor cursors stay in code points and are
  converted to cells only when drawn.
- **Content is not translated.** Box names and texts are user data. Only the welcome project is
  built in the current language the first time it's created.

## Import and export

`storage/exchange` converts between a `Canvas` and the exchange formats. Every outline importer
(Markdown, OPML, plain text) parses into the same small intermediate form:

```text
 .md / .opml / .txt ──parse──► Outline { title, items (tree), links (by title) }
                                   │ buildCanvas
                                   ▼
                               Canvas ──arrangeTree──► laid-out boxes
```

so adding a format means writing one parser, and layout and link resolution are shared. Links
are resolved by title (first match), and links to unknown titles are dropped. The bundle skips
the outline: it is the project's own JSON content in one file and restores it exactly.

`model/arrange` lays out a tree that has no geometry: leaves are sized to their text (clamped
to a readable width), children are packed into rows inside their parent below its text, and
roots into rows on the canvas, aiming at a roughly 2:1 shape. Reading order is kept, so the
sidebar outline matches the source file.

## Settings and recent projects

`Settings` (`settings.json`) holds the language, the sidebar width and up to twelve recent
projects, most recent first, compared by normalized path (case-insensitive on Windows). A
project is moved to the front when it is opened, imported, saved as, switched to or left open
on quit. At startup the first recent project that still exists is opened, and only on the very
first launch the welcome project.

## Storage

`JsonStorage` stores one directory per project and one file per node (see the README for the
layout). Notes on the implementation:

- **Atomic writes.** Each file is written to `*.tmp` and then renamed over the target.
  `config.json`, which marks a directory as a project, is written last.
- **Incremental auto-save.** `Canvas` reports a `ChangeSet` with the changed nodes, the
  removed nodes, a config flag (camera, name, links) and a full flag (content replaced by
  undo/redo or load). `ProjectManager` turns these into `saveNode`, `removeNode` and
  `saveConfig` calls, and only the full flag rewrites everything.
- **Always persisted.** A new project gets its path when it's created. The first edit writes it
  in full, and every edit after that is saved incrementally, so there is no unsaved state. An
  untouched new project never creates a folder.
- **Locking.** `.lock` holds the owning process id and a session token. Opening a project
  whose lock belongs to another live process shows a warning. A lock left behind by a crash
  (its process is gone) is silently taken over.
- **Loading** parses everything first and only touches the target canvas once nothing can
  fail any more, so a failed revert leaves the document intact.
- **Versioning.** `"format": "1.1"`. A reader accepts the same major version and treats a
  missing field as `1.0` (the original format, still readable).
- **Numbers** are written with the shortest representation that round-trips exactly. The
  original code used the stream default of 6 significant digits, which moved boxes on every save.
