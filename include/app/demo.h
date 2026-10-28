#ifndef IC_DEMO_H
#define IC_DEMO_H

#include <string>

namespace ic {

class Canvas;

// Fills `canvas` with the welcome/tutorial project shown on first launch:
// nested boxes, connections and a short guide to the controls.
void buildDemoCanvas(Canvas& canvas);

// Renders a project to plain text at the given console size, with the
// view fitted to its content (no console needed). Used by --snapshot and
// by the renderer tests.
std::string renderSnapshot(Canvas& canvas, int width, int height);

}  // namespace ic

#endif  // IC_DEMO_H
