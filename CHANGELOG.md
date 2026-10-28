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

### Added
- A JSON parser and serializer (`util/json`), and UTF-8 utilities.
- Project format 1.1 with an explicit version. Format 1.0 files still open.
- `--snapshot` headless rendering, plus `--version` and `--help`.
- Confirmation before quitting with an unsaved project.
- Unit tests, a CMake test target, and CI on MSVC, GCC and Clang with sanitizers.

## 0.1.0

The initial console prototype.
