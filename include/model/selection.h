#ifndef IC_SELECTION_H
#define IC_SELECTION_H

#include <string>
#include <vector>

namespace ic {

// Free-form rectangular selection (not line-based)
// Covers an area on the canvas
class Selection {
public:
    bool active = false;
    double startX = 0.0;
    double startY = 0.0;
    double endX = 0.0;
    double endY = 0.0;

    void begin(double x, double y) {
        active = true;
        startX = x;
        startY = y;
        endX = x;
        endY = y;
    }

    void update(double x, double y) {
        if (active) {
            endX = x;
            endY = y;
        }
    }

    void clear() {
        active = false;
        startX = startY = endX = endY = 0.0;
    }

    // Get normalized bounds
    double left()   const { return std::min(startX, endX); }
    double right()  const { return std::max(startX, endX); }
    double top()    const { return std::min(startY, endY); }
    double bottom() const { return std::max(startY, endY); }

    bool contains(double x, double y) const {
        return x >= left() && x <= right() && y >= top() && y <= bottom();
    }
};

} // namespace ic

#endif // IC_SELECTION_H
