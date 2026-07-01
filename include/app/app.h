#ifndef IC_APP_H
#define IC_APP_H

#include <string>
#include <memory>
#include <functional>
#include <optional>

namespace ic {

class Canvas;
class IRenderer;
class IStorage;
class InputHandler;
class ProjectManager;
class ProjectMenu;

// Main application controller.
// Coordinates model, view, input, and storage.
//
// Architecture:
//   App owns:
//     - Canvas (model)
//     - IRenderer (view, pluggable)
//     - IStorage (storage, pluggable)
//     - InputHandler (input processing)
//
//   App loop:
//     1. Process input events
//     2. Render canvas
//     3. Flush renderer
//     4. Repeat until quit

class App {
public:
    App();
    ~App();

    // Initialize the application
    bool initialize(int viewWidth = 120, int viewHeight = 40);

    // Set renderer (pluggable: ConsoleRenderer, QtRenderer, etc.)
    void setRenderer(std::unique_ptr<IRenderer> renderer);

    // Set project manager
    void setProjectManager(std::unique_ptr<ProjectManager> pm);
    ProjectManager* projectManager() { return projectManager_.get(); }
    const ProjectManager* projectManager() const { return projectManager_.get(); }

    // --- Project operations (delegated to ProjectManager) ---
    std::string createProject(const std::string& name = "Untitled");
    std::string openProject(const std::string& path);
    bool closeProject(const std::string& uuid);
    bool saveCurrent();
    bool saveAs(const std::string& newPath);
    bool revert();

    // Legacy: create new project
    void newCanvas(const std::string& name = "Untitled");

    // Main loop (blocking)
    void run();

    // Process a single frame (for integration with other loops)
    bool processFrame();

    // Quit the application
    void quit();

    // Get current active canvas (may be null)
    Canvas* canvas();
    const Canvas* canvas() const;

    // Get renderer
    IRenderer* renderer() { return renderer_.get(); }
    const IRenderer* renderer() const { return renderer_.get(); }

    // Get input handler (may be null if no active project)
    InputHandler* inputHandler() { return inputHandler_.get(); }

    // Check if running
    bool isRunning() const { return running_; }

    // Callbacks
    using OnInitCallback = std::function<void(App&)>;
    using OnFrameCallback = std::function<void(App&)>;
    using OnQuitCallback = std::function<void(App&)>;

    void onInit(OnInitCallback cb) { onInit_ = cb; }
    void onFrame(OnFrameCallback cb) { onFrame_ = cb; }
    void onQuit(OnQuitCallback cb) { onQuit_ = cb; }

private:
    std::unique_ptr<ProjectManager> projectManager_;
    std::unique_ptr<IRenderer> renderer_;
    std::unique_ptr<InputHandler> inputHandler_;
    std::unique_ptr<ProjectMenu> projectMenu_;

    bool running_ = false;

    OnInitCallback onInit_;
    OnFrameCallback onFrame_;
    OnQuitCallback onQuit_;

    void renderOnce();
    void rebuildInputHandler();
    void showProjectMenu(int mx, int my);
    void zoomToNode(const std::string& uuid);
};

} // namespace ic

#endif // IC_APP_H
