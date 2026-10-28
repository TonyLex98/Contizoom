#ifndef IC_CONSOLE_OUTPUT_H
#define IC_CONSOLE_OUTPUT_H

#include "view/cell_buffer.h"

namespace ic {

// Presents a CellBuffer on the Windows console in a single
// WriteConsoleOutputW call (flicker-free full-frame update).
class ConsoleOutput {
public:
    ConsoleOutput();
    void present(const CellBuffer& buffer);

    // Draws only a rectangular part of the buffer at (x, y) on screen;
    // used by popup menus drawn over the current frame.
    void presentRegion(const CellBuffer& buffer, int x, int y);

private:
    void* hOut_ = nullptr;
};

}  // namespace ic

#endif  // IC_CONSOLE_OUTPUT_H
