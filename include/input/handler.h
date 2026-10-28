#ifndef IC_INPUT_HANDLER_H
#define IC_INPUT_HANDLER_H

#include "input/input_event.h"
#include "model/canvas.h"
#include "view/geometry.h"
#include "view/ui_state.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ic {

enum class InputResult { Handled, NotHandled, RequestQuit, RequestSave, RequestNew, RequestMenu, SidebarNodeClick };

// Turns raw screen-space input into edits of the Canvas and UiState.
// Hit testing uses the screen rects of the last rendered frame, so what
// can be clicked is exactly what was drawn. Platform independent.
class InputHandler {
public:
    InputHandler(Canvas& canvas, UiState& ui);

    InputResult process(const InputEvent& event);

    // Layout of the last rendered frame.
    void setLayout(const std::map<std::string, ScreenRect>& screenRects,
                   const std::map<std::string, ScreenRect>& sidebarRects, const CanvasArea& area);

    const std::string& sidebarClickedUuid() const { return sidebarClicked_; }
    bool isEditing() const { return ui_.nodeEditor.active || ui_.connEditor.active; }

    Canvas& canvas() { return canvas_; }

    // Camera helpers (also used by the app for sidebar navigation).
    void zoomToNode(const std::string& uuid);
    void fitAll();

private:
    Canvas& canvas_;
    UiState& ui_;

    std::map<std::string, ScreenRect> screenRects_;
    std::map<std::string, ScreenRect> sidebarRects_;
    CanvasArea area_ = CanvasArea::forConsole(120, 40);
    std::string sidebarClicked_;

    // Mouse gesture in progress.
    enum class Gesture { None, PendingClick, DragNodes, Pan, Resize, Connect, AreaSelect };
    Gesture gesture_ = Gesture::None;
    int pressX_ = 0, pressY_ = 0;
    int lastX_ = 0, lastY_ = 0;
    std::string pressNode_;
    bool pressWasSelected_ = false;

    struct DragStart {
        Rect rect;
        double unitsPerCellX = 1, unitsPerCellY = 1;  // canvas units per screen cell
    };
    std::map<std::string, DragStart> drag_;

    struct ResizeStart {
        std::string uuid;
        ResizeCorner corner = ResizeCorner::None;
        Rect rect;
        double unitsPerCellX = 1, unitsPerCellY = 1;
        double minW = 6, minH = 3;
        std::map<std::string, Rect> descendants;  // subtree rescaled with the box
    };
    ResizeStart resize_;

    Anchor connectFromAnchor_;

    // Double-click detection.
    uint64_t lastClickTime_ = 0;
    int lastClickX_ = -1000, lastClickY_ = -1000;
    std::string lastClickTarget_;

    // Dispatch.
    InputResult onKey(const InputEvent& e);
    InputResult onMouseDown(const InputEvent& e);
    InputResult onMouseMove(const InputEvent& e);
    InputResult onMouseUp(const InputEvent& e);
    InputResult onScroll(const InputEvent& e);

    // Coordinates.
    double toWorldX(double sx) const;
    double toWorldY(double sy) const;
    bool unitsPerCell(const Node& node, double& ux, double& uy) const;

    // Hit testing.
    std::shared_ptr<Node> nodeAt(int x, int y) const;
    std::string connectionAt(int x, int y) const;
    ResizeCorner resizeCornerAt(int x, int y, std::string& uuid) const;
    Anchor anchorAt(int x, int y, std::string& uuid) const;
    bool isDoubleClick(const std::string& target, int x, int y, uint64_t now) const;
    void updateHover(int x, int y);

    // Actions.
    void select(const std::string& uuid, bool additive);
    std::vector<std::string> topLevelSelection() const;
    void beginDrag();
    void beginResize(const std::string& uuid, ResizeCorner corner);
    void updateResize(int x, int y);
    void finishAreaSelect();
    void createNode();
    void deleteSelection();
    void promoteSelection();
    void demoteSelection();
    void frameSelection();
    void pan(double cellsX, double cellsY);
    void zoomAt(double factor, double sx, double sy);

    // Node editor.
    void startEditing(const std::string& uuid, bool name);
    void editorKey(const InputEvent& e);
    void commitEdit();
    void cancelEdit();

    // Connection label editor.
    void startConnectionEdit(const std::string& connUuid, int x, int y);
    void connectionEditorKey(const InputEvent& e);
    void commitConnectionEdit();
};

}  // namespace ic

#endif  // IC_INPUT_HANDLER_H
