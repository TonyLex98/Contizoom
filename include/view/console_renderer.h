#ifndef IC_CONSOLE_RENDERER_H
#define IC_CONSOLE_RENDERER_H

#include "model/canvas.h"
#include "view/cell_buffer.h"
#include "view/geometry.h"
#include "view/ui_state.h"

#include <map>
#include <set>
#include <string>

namespace ic {

// Text shown in the status rows that isn't part of the document.
struct HudInfo {
    std::string projectName;
    bool unsaved = false;        // project has no file yet
    std::string status;          // last action result, e.g. "Saved"
    bool statusIsError = false;
};

// Composes a frame of the canvas into a CellBuffer. Platform independent:
// presenting the buffer on a real console is ConsoleOutput's job.
//
// Layout is "fractal": root boxes are placed by the viewport transform,
// children are laid out proportionally inside their parent's on-screen
// box. As you zoom out, boxes degrade from full boxes with name and text,
// to compact boxes, to single-cell markers; a parent that has no interior
// room left stands in for its whole subtree.
class ConsoleRenderer {
public:
    void resize(int width, int height);
    void render(const Canvas& canvas, const UiState& ui, const HudInfo& hud);

    const CellBuffer& buffer() const { return buffer_; }
    int width() const { return buffer_.width(); }
    int height() const { return buffer_.height(); }

    // Where each node ended up on screen this frame (for hit testing).
    const std::map<std::string, ScreenRect>& screenRects() const { return screenRects_; }
    const std::map<std::string, ScreenRect>& sidebarRects() const { return sidebarRects_; }
    const CanvasArea& canvasArea() const { return canvasArea_; }
    // Where the in-place editor was drawn (invalid when not editing).
    const ScreenRect& editorRect() const { return editorRect_; }

private:
    CellBuffer buffer_;
    CanvasArea canvasArea_;
    std::map<std::string, ScreenRect> screenRects_;
    std::map<std::string, ScreenRect> sidebarRects_;
    ScreenRect editorRect_;

    ScreenRect computeScreenRect(const Node& node, const Canvas& canvas);
    void drawNode(const Node& node, const ScreenRect& sr, const UiState& ui, bool isCluster);
    void drawMarker(const Node& node, const ScreenRect& sr, NodeState state, bool isCluster);
    void drawNodeName(const Node& node, const ScreenRect& sr, const UiState& ui);
    void drawConnection(const Connection& conn, const ScreenRect& from, const ScreenRect& to, bool selected,
                        bool dim);
    void drawConnectionPreview(const Canvas& canvas, const UiState& ui);
    void drawConnectionLabel(const Connection& conn, const ScreenRect& from, const ScreenRect& to, bool selected);
    void drawAreaSelection(const AreaSelection& sel, const Viewport& vp);
    void drawNodeEditor(const NodeEditor& editor);
    void drawConnectionEditor(const ConnectionEditor& editor);
    void drawField(int x, int y, int cells, const std::string& text, int cursor, uint16_t attr);
    void drawHud(const Canvas& canvas, const UiState& ui, const HudInfo& hud);
    void drawSidebar(const Canvas& canvas, const UiState& ui);
    void drawSidebarTree(const Canvas& canvas, const UiState& ui, const std::string& parentUuid, int indent, int& row);
};

}  // namespace ic

#endif  // IC_CONSOLE_RENDERER_H
