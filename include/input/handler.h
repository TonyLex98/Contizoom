#ifndef IC_INPUT_HANDLER_H
#define IC_INPUT_HANDLER_H

#include "input/history.h"
#include "input/input_event.h"
#include "model/canvas.h"
#include "view/geometry.h"
#include "view/ui_state.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ic {

enum class InputResult {
    Handled,
    NotHandled,
    RequestQuit,
    RequestSave,
    RequestNew,
    RequestMenu,
    SidebarNodeClick,
    LayoutChanged,  // the sidebar was resized or toggled (worth saving)
};

// What a right-click landed on, so the app can show the matching menu.
struct ContextTarget {
    enum class Kind { Canvas, Node, Connection } kind = Kind::Canvas;
    std::string uuid;
    int x = 0, y = 0;  // screen cell of the click
};

// Editing commands, available from keys and from context menus.
enum class Action {
    NewBox,
    NewBoxHere,  // at the last right-click (inside the box under it, if any)
    EditText,
    Rename,
    Delete,
    Frame,
    Promote,
    Demote,
    ZoomTo,
    FitAll,
    ConnectionRename,
    ConnectionReverse,
    ConnectionNextStyle,
    ConnectionDelete,
    Undo,
    Redo,
};

// Turns raw screen-space input into edits of the Canvas and UiState.
// Hit testing uses the screen rects of the last rendered frame, so what
// can be clicked is exactly what was drawn. Every edit goes through the
// undo history. Platform independent.
class InputHandler {
public:
    InputHandler(Canvas& canvas, UiState& ui);

    InputResult process(const InputEvent& event);

    // Layout of the last rendered frame.
    void setLayout(const std::map<std::string, ScreenRect>& screenRects,
                   const std::map<std::string, ScreenRect>& sidebarRects, const CanvasArea& area,
                   const ScreenRect& editorRect = ScreenRect{});

    const std::string& sidebarClickedUuid() const { return sidebarClicked_; }
    const ContextTarget& contextTarget() const { return context_; }
    bool isEditing() const { return ui_.nodeEditor.active || ui_.connEditor.active; }

    void perform(Action action);
    void setConnectionStyle(ConnType type);  // for the selected connection
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void clearHistory() { history_.clear(); }
    const History& history() const { return history_; }

    // A message for the status bar ("Undone: move"), once.
    std::string takeStatus();

    Canvas& canvas() { return canvas_; }

    // Camera helpers (also used by the app for sidebar navigation).
    void zoomToNode(const std::string& uuid);
    void fitAll();

private:
    Canvas& canvas_;
    UiState& ui_;
    History history_;

    std::map<std::string, ScreenRect> screenRects_;
    std::map<std::string, ScreenRect> sidebarRects_;
    CanvasArea area_ = CanvasArea::forConsole(120, 40);
    ScreenRect editorRect_;
    std::string sidebarClicked_;
    ContextTarget context_;

    // Mouse gesture in progress.
    enum class Gesture { None, PendingClick, DragNodes, Pan, Resize, Connect, AreaSelect, Divider };
    Gesture gesture_ = Gesture::None;
    int pressX_ = 0, pressY_ = 0;
    int lastX_ = 0, lastY_ = 0;
    std::string pressNode_;
    std::string gestureSnapshot_;  // document before a drag/resize, for undo
    std::string status_;

    // A new box and the name typed into it right away are one undo step:
    // the name is folded into the "new box" step as long as nothing else
    // was recorded in between.
    std::string newBox_;
    uint64_t newBoxVersion_ = 0;

    // Drags and resizes work in world coordinates: the parent may grow
    // left/up under the box while it's being dragged (which shifts the box's
    // parent-relative coordinates), and the box must still follow the mouse.
    struct DragStart {
        Rect world;
        double unitsPerCellX = 1, unitsPerCellY = 1;  // canvas units per screen cell
    };
    std::map<std::string, DragStart> drag_;

    struct ResizeStart {
        std::string uuid;
        ResizeCorner corner = ResizeCorner::None;
        Rect world;
        double unitsPerCellX = 1, unitsPerCellY = 1;
        double minW = 6, minH = 3;
        std::map<std::string, Rect> descendants;  // subtree rescaled with the box
    };
    ResizeStart resize_;

    Anchor connectFromAnchor_;
    int expandedSidebarWidth_ = CanvasArea::kSidebarWidth;  // restored by double-click after collapsing

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
    Anchor anchorAt(int x, int y, std::string& uuid, bool anyBox) const;
    bool isDoubleClick(const std::string& target, int x, int y, uint64_t now) const;
    bool onDivider(int x, int y) const;
    void updateHover(int x, int y);

    // Runs an edit and records it as one undo step if it changed anything.
    void edit(Msg label, const std::function<void()>& change);
    void undoRedo(bool redo);
    void afterContentReplaced();
    // A drag or resize whose mouse-up never came (released outside the
    // window, or a key pressed mid-drag) is completed and recorded.
    void finishGesture();
    void cancelGesture();  // Escape mid-drag: put everything back
    void boxCreated(const std::string& uuid);

    // Actions.
    void select(const std::string& uuid, bool additive);
    void selectConnection(const std::string& uuid);
    std::vector<std::string> topLevelSelection() const;
    void beginDrag();
    void placeAtWorld(Node& node, double worldX, double worldY);
    void beginResize(const std::string& uuid, ResizeCorner corner);
    void updateResize(int x, int y);
    void finishConnect(int x, int y);
    void finishAreaSelect();
    void createBox();
    void createBoxAt(int screenX, int screenY, const std::string& parentId);
    void pan(double cellsX, double cellsY);
    void zoomAt(double factor, double sx, double sy);
    std::pair<int, int> connectionLabelCell(const std::string& connUuid) const;

    // Node editor.
    void startEditing(const std::string& uuid, bool name);
    void editorKey(const InputEvent& e);
    void commitEdit();
    bool editorUndoKey(const InputEvent& e, TextUndo& history, std::string& text, int& row, int& col);
    void editorTextChanged(TextUndo& history, TextUndo::State before, TextUndo::Kind kind, bool newGroup);

    // Connection label editor.
    void startConnectionEdit(const std::string& connUuid, int x, int y);
    void connectionEditorKey(const InputEvent& e);
    void commitConnectionEdit();
};

}  // namespace ic

#endif  // IC_INPUT_HANDLER_H
