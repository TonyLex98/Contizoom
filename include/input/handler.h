#ifndef IC_INPUT_HANDLER_H
#define IC_INPUT_HANDLER_H

#include <string>
#include <functional>
#include <optional>
#include <vector>
#include <cstdint>
#include "model/node.h"
#include "view/console_renderer.h"

namespace ic {

class Canvas;

// Input event types
enum class InputType {
    KeyPress,       // Keyboard key down
    KeyRelease,     // Keyboard key up
    MouseMove,      // Mouse movement
    MouseDown,      // Mouse button press
    MouseUp,        // Mouse button release
    MouseScroll,    // Mouse wheel
    Resize,         // Window resize
    Quit            // Exit request
};

// Modifiers
struct Modifiers {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

// Raw input event from input device.
// All coordinates are SCREEN coordinates (console cells 0..width-1, 0..height-1).
struct InputEvent {
    InputType type;
    Modifiers mods;
    char key = 0;       // Virtual key code
    wchar_t ch = 0;     // Unicode character (0 if not printable)
    int mouseX = 0;
    int mouseY = 0;
    int button = 0;
    double scrollY = 0;
    int width = 0;
    int height = 0;
};

// Action result
enum class InputResult {
    Handled,
    NotHandled,
    RequestQuit,
    RequestSave,
    RequestNew,
    SidebarNodeClick
};

// Input handler: converts raw screen events into canvas model modifications.
// Screen coords (console cells) → logical coords (canvas space) → modify Canvas.

class InputHandler {
public:
    explicit InputHandler(Canvas& canvas);

    InputResult process(const InputEvent& event);

    void setViewSize(int w, int h) { viewWidth_ = w; viewHeight_ = h; sidebarWidth_ = 26; canvasArea_ = CanvasArea::withSidebar(w, h); }
    int getViewWidth()  const { return viewWidth_; }
    int getViewHeight() const { return viewHeight_; }

    void setPanSpeed(double speed) { panSpeed_ = speed; }
    void setScreenRects(const std::map<std::string, ScreenRect>& rects) { screenRects_ = rects; }
    void setSidebarNodeRects(const std::map<std::string, ScreenRect>& rects) { sidebarNodeRects_ = rects; }
    void setCanvasArea(const CanvasArea& area) { canvasArea_ = area; }
    const std::string& sidebarClickedUuid() const { return sidebarNodeUuid_; }
    bool isDragging() const { return mouseDown_.left || mouseDown_.right; }
    Canvas& canvas() { return canvas_; }
    const Canvas& canvas() const { return canvas_; }

private:
    Canvas& canvas_;
    int viewWidth_ = 120;
    int viewHeight_ = 40;
    double panSpeed_ = 1.0;
    std::map<std::string, ScreenRect> screenRects_; // From renderer, for hit testing
    std::map<std::string, ScreenRect> sidebarNodeRects_; // From renderer, for sidebar hit testing
    int sidebarWidth_ = 26; // Sidebar width in console columns
    std::string sidebarNodeUuid_; // UUID of node clicked in sidebar (reset each process)
    CanvasArea canvasArea_; // Canvas rendering area (shared with renderer)

    struct { bool left = false; bool right = false; } mouseDown_;
    double lastLogicalX_ = 0, lastLogicalY_ = 0;
    int lastScreenX_ = 0, lastScreenY_ = 0;
    bool zHeld_ = false;
    bool draggingNodes_ = false;
    double dragOffsetX_ = 0, dragOffsetY_ = 0;

    // Per-node drag state for consistent 1:1 movement across zoom levels
    struct DragState {
        double startRelX = 0, startRelY = 0;  // node's starting relative position
        double startScreenX = 0, startScreenY = 0;  // mouse screen pos at drag start
        double parentSrX = 0, parentSrY = 0;  // parent screen rect origin
        double parentSrW = 1, parentSrH = 1;  // parent screen rect size
        double parentRectW = 1, parentRectH = 1;  // parent logical size
        bool isChild = false;  // false = root node (world coords)
    };
    std::map<std::string, DragState> dragStates_;

    // Inline text editing
    std::optional<std::string> editingNode_; // UUID of node being edited
    std::string editText_; // Current text being edited
    bool editingName_ = false; // true = editing node name, false = editing node text
    int editCursorCol_ = 0;
    int editCursorRow_ = 0;
    Rect editNodeRect_; // Original rect for rendering overlay

    // Double-click detection
    int lastClickX_ = -1, lastClickY_ = -1;
    uint64_t lastClickTime_ = 0;
    std::optional<std::string> lastClickedNode_;
    std::optional<std::string> connectionFrom_;

    // Input dispatching
    InputResult handleKeyPress(const InputEvent&);
    InputResult handleKeyRelease(const InputEvent&);
    InputResult handleMouseMove(const InputEvent&);
    InputResult handleMouseDown(const InputEvent&);
    InputResult handleMouseUp(const InputEvent&);
    InputResult handleMouseScroll(const InputEvent&);
    InputResult handleResize(const InputEvent&);

    // Screen ↔ Logical conversion
    double screenToLogicalX(double sx) const;
    double screenToLogicalY(double sy) const;
    double logicalToScreenX(double lx) const;
    double logicalToScreenY(double ly) const;

    // Actions
    void selectNode(const std::string& uuid, bool add = false);
    void selectArea(double x1, double y1, double x2, double y2);
    void createFrame();
    void deleteSelected();
    void promoteSelected();
    void demoteSelected();
    void createNodeAtCursor();
    void renameSelected();
    void autoCenterViewportOnRoot();
    void startConnection(const std::string& fromUuid);
    void cancelConnection();

    // Hit testing using screen rects (for fractal layout)
    std::shared_ptr<Node> findNodeAtScreen(int screenX, int screenY) const;

    // Inline editing
    void startEditing(const std::string& nodeUuid);
    void startEditingName(const std::string& nodeUuid);
    void handleEditKey(const InputEvent& event);
    void commitEdit();
    void cancelEdit();

    // UTF-8 helpers (editCursorCol_ is character index, not byte index)
    static int utf8CharLen(const std::string& s, int bytePos); // bytes in char at bytePos
    static int utf8CharToByte(const std::string& s, int charIdx); // byte pos for char index
    static int utf8CharCount(const std::string& s); // number of chars
};

} // namespace ic

#endif // IC_INPUT_HANDLER_H
