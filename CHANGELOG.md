# Changelog

## 0.2.0

A full review and rework of the codebase.

### Fixed
- Moving a box into its own subtree (Shift+Tab, frames) created a parent cycle. That hung the
  program through infinite recursion and corrupted saved projects. The model now rejects such
  moves and repairs cycles in damaged files.
- Promote (Tab) left boxes at wrong coordinates and with stale depths. Demote (Shift+Tab) only
  changed a number without changing the hierarchy. Frames kept members in their old parent's
  child list.
- Deleting a box left its children orphaned with a dangling parent link. Delete now removes
  the whole subtree.
- Coordinates were saved with 6 significant digits, so boxes drifted on every save. Strings
  with control characters produced invalid JSON. Saves weren't atomic.
- Load errors and save errors were printed with `std::cerr` over the full-screen UI. They now
  show in the status bar.
- The right-click menu closed on the first key release, the arrow keys didn't work in it
  (`H`/`P` did instead), mouse clicks on items did nothing, and its `→` and `─` glyphs were
  printed as garbage. "Open project…" and "Save as…" did nothing.
- Every temporary `ConsoleInput` "restored" the console on destruction. Opening the menu could
  disable mouse input and leave a 9999-line scroll buffer.
- Mouse moves were reordered after clicks in the same batch, so drags ended at the wrong spot.
- `+` and `-` never zoomed, because the virtual key code was compared to the character.
  `W`/`S` panned opposite to `A`/`D`. A single click didn't select a box.
- Dragging a parent together with its selected child moved the child twice.
- Rubber-band selection was unreachable, because the menu took the right button.
- Clicks on connections were tested against a straight center line instead of the drawn route.
- Non-ASCII text: node widths were measured in bytes, the sidebar printed UTF-8 bytes as
  characters, truncated sequences were read past the end of the string, and Cyrillic project
  names were saved as "Untitled".
- `ProjectManager::closeAll()` erased from the vector it was iterating over.

### Changed
- UI state (selection, hover, editors) moved from the model into `UiState`.
- The renderer composes into a portable `CellBuffer`. Win32 code is isolated in `platform/`.
- Shared screen geometry (`view/geometry.h`) is used by both drawing and hit-testing.
- Unicode box drawing, a context-sensitive help row, a status bar, and an outline sidebar that
  shows box names.
- Connections leave from the side facing their target and never cut through their own boxes.
- The welcome project lives in the projects folder instead of next to the executable.
- The separate "zoom level" concept was removed. There is one zoom, plus fit-all (`0`) and
  zoom-to-selection (`Enter`).
- A plain click on a selected box no longer opens the editor (use a double-click, `E` or `F2`).
  The text editor is sized to its text instead of covering the whole box with its children.
- Anchor points only appear on the hovered or selected box, and on every box while a link is
  being dragged. Before, nine dots sat on every box.
- New projects are saved from their first edit on, with no "unsaved" state. Auto-save writes
  only the changed boxes. A camera move used to rewrite every box file.

### Added
- Interface in English, Russian, Chinese, French and Spanish: one compile-checked message
  catalog, runtime switching from the menu, `--lang`, a saved choice, and system language
  detection. The welcome project is created in the chosen language. Double-width (CJK)
  characters are supported throughout layout, editing and console output.
- Parents grow to the left and upwards when a child is dragged past their border.
- A new box at a spot: double-click empty canvas, or "New box here" in the right-click menu.
- A JSON parser and serializer (`util/json`), and UTF-8 utilities.
- Project format 1.1 with an explicit version. Format 1.0 files still open.
- `--snapshot` headless rendering, plus `--version` and `--help`.
- Undo/redo for every edit (`Ctrl+Z` / `Ctrl+Y`), kept per open project.
- Connections: a live preview while dragging, styles (`T`), reverse (`V`) and rename (`F2`).
  Duplicate links are prevented, and links to boxes collapsed by zoom are shown between their
  visible ancestors. A click on a line inside a box selects the line, not the box.
- Context menus for boxes, connections and the project.
- A project lock that warns when the same project is open in two windows, "Save a copy as",
  Markdown export, and an Open list showing the date and number of boxes.
- Unit tests (80 cases), a CMake test target, and CI on MSVC, GCC and Clang with sanitizers.

## 0.1.0

The initial console prototype.
