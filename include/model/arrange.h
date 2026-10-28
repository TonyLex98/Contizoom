#ifndef IC_ARRANGE_H
#define IC_ARRANGE_H

namespace ic {

class Canvas;

// Lays out a whole canvas from scratch, ignoring existing positions: every
// box is sized to its text, children are arranged in rows inside their
// parent (below its text), and roots in rows on the canvas. Used for
// imported outlines, which have structure but no geometry.
void arrangeTree(Canvas& canvas);

}  // namespace ic

#endif  // IC_ARRANGE_H
