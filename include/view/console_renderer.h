#ifndef IC_CONSOLE_RENDERER_H
#define IC_CONSOLE_RENDERER_H

#include "view/renderer.h"
#include "model/node.h"
#include "model/connection.h"
#include "model/canvas.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace ic {

struct ScreenRect {
    double x = 0, y = 0, w = 0, h = 0;
    double centerX() const { return x + w / 2; }
    double centerY() const { return y + h / 2; }
    bool valid() const { return w > 0 && h > 0; }
};

// Canvas rendering area: defines the region where canvas content is drawn.
// Widgets (sidebar, HUD) render outside or over this area but don't affect
// canvas coordinate transforms. Changing widget layout only requires updating
// this struct — canvas rendering stays consistent.
struct CanvasArea {
    int left = 0;    // First usable column (e.g. 26 for sidebar)
    int top = 0;     // First usable row (e.g. 1 for HUD)
    int right = 0;   // Last usable column
    int bottom = 0;  // Last usable row

    double centerX() const { return (left + right) / 2.0; }
    double centerY() const { return (top + bottom) / 2.0; }
    int width() const { return right - left + 1; }
    int height() const { return bottom - top + 1; }
    bool contains(int x, int y) const {
        return x >= left && x <= right && y >= top && y <= bottom;
    }

    static CanvasArea full(int vw, int vh) {
        return {0, 0, vw - 1, vh - 1};
    }
    // Default: sidebar (26) on left, HUD top/bottom rows
    static CanvasArea withSidebar(int vw, int vh) {
        return {26, 1, vw - 1, vh - 2};
    }
};

class ConsoleRenderer : public IRenderer {
public:
    ConsoleRenderer();
    void initialize(int width = 120, int height = 40);

    void render(const Canvas& canvas) override;
    void renderNode(const Node& node, int screenX, int screenY) override;
    void renderConnection(const Connection& conn, const Node& from, const Node& to) override;
    void renderSelection(const Selection& sel, const Viewport& vp, double vw, double vh) override;
    void renderHUD(const Viewport& vp, double vw, double vh) override;
    void renderFrame(const std::vector<std::shared_ptr<Node>>& nodes, const std::string& label) override;
    void clear() override;
    void flush() override;
    int getViewWidth()  const override { return viewWidth_; }
    int getViewHeight() const override { return viewHeight_; }

  // Get computed screen rects (for hit testing, etc)
    const std::map<std::string, ScreenRect>& screenRects() const { return screenRects_; }
    const std::map<std::string, ScreenRect>& sidebarNodeRects() const { return sidebarNodeRects_; }
    const CanvasArea& canvasArea() const { return canvasArea_; }
    void setCanvasArea(const CanvasArea& area) { canvasArea_ = area; }

private:
    void* hOut_ = nullptr;
    struct Cell { wchar_t ch; uint16_t attr; };
    std::vector<Cell> buffer_;
    int viewWidth_ = 120;
    int viewHeight_ = 40;

    // Cache of screen rects per node uuid (recomputed each frame)
    std::map<std::string, ScreenRect> screenRects_;
    std::map<std::string, ScreenRect> sidebarNodeRects_;

    // Canvas area for coordinate transforms
    CanvasArea canvasArea_;

    void fillBuffer();
    void putChar(int x, int y, wchar_t c);
    void putAttr(int x, int y, uint16_t attr);

    ScreenRect computeScreenRect(const Node* node, const Canvas& canvas,
                                   const Viewport& vp, int vw, int vh);
    void drawNodeAt(const Node& node, const ScreenRect& sr, int vw, int vh);
    void drawConnectionAt(const Connection* conn, const ScreenRect& from, const ScreenRect& to,
                           int vw, int vh);
    void drawSelection(const Selection& sel, int vw, int vh, const Viewport& vp);
    void drawHUD(const Viewport& vp, int vw, int vh);
    void drawEditOverlay(const EditOverlay& overlay, int vw, int vh, const Viewport& vp);
    void drawSidebar(const Canvas& canvas);
    void drawSidebarNodeTree(const Canvas& canvas, const std::string& parentUuid, int indent, int& row);
};

} // namespace ic

#endif // IC_CONSOLE_RENDERER_H
