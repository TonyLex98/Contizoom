#ifndef IC_RENDERER_H
#define IC_RENDERER_H

#include <string>
#include <vector>
#include <memory>
#include <map>

namespace ic {

class Node;
class Connection;
class Canvas;
struct Viewport;
class Selection;

// Abstract renderer interface for pluggable backends
// Implement for: Console (ANSI/VT100), Qt, SDL, etc.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Full re-render of the canvas
    virtual void render(const Canvas& canvas) = 0;

    // Render a single node
    virtual void renderNode(const Node& node, int screenX, int screenY) = 0;

    // Render a connection between two nodes
    virtual void renderConnection(const Connection& conn,
                                   const Node& fromNode,
                                   const Node& toNode) = 0;

    // Render selection rectangle
    virtual void renderSelection(const Selection& sel,
                                  const Viewport& vp,
                                  double viewWidth,
                                  double viewHeight) = 0;

    // Render the HUD (zoom level, position, help)
    virtual void renderHUD(const Viewport& vp,
                            double viewWidth,
                            double viewHeight) = 0;

    // Render frame around a group of nodes
    virtual void renderFrame(const std::vector<std::shared_ptr<Node>>& nodes,
                              const std::string& label) = 0;

    // Clear the screen
    virtual void clear() = 0;

    // Flush output
    virtual void flush() = 0;

    // Get view dimensions
    virtual int getViewWidth()   const = 0;
    virtual int getViewHeight()  const = 0;
};

} // namespace ic

#endif // IC_RENDERER_H
