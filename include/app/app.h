#ifndef IC_APP_H
#define IC_APP_H

#include "app/project_manager.h"
#include "input/handler.h"
#include "platform/console_input.h"
#include "platform/console_output.h"
#include "view/console_renderer.h"
#include "view/ui_state.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace ic {

// Interactive console application (Windows). Wires the pieces together:
//
//   ConsoleInput --events--> InputHandler --edits--> Canvas / UiState
//                                                        |
//   ConsoleOutput <--CellBuffer-- ConsoleRenderer <------+
//
// ProjectManager owns the open documents and auto-saves them.
class App {
public:
    App();
    ~App();

    ProjectManager& projects() { return projects_; }

    // What opens when the program starts without arguments: the most
    // recently used project, or the welcome project on first launch.
    void openStartupProject();
    void openDemo();

    // Records the active project at the top of the recent list.
    void rememberActive();

    // Runs until the user quits. Returns the process exit code.
    int run();

private:
    ProjectManager projects_;
    UiState ui_;
    ConsoleInput input_;
    ConsoleOutput output_;
    ConsoleRenderer renderer_;
    // One handler (and so one undo history) per open project.
    std::map<std::string, std::unique_ptr<InputHandler>> handlers_;
    InputHandler* handler_ = nullptr;
    std::string boundProject_;
    Canvas placeholder_;  // shown when no project is open

    bool running_ = false;
    std::string status_;
    bool statusIsError_ = false;
    uint64_t statusTime_ = 0;

    Canvas& currentCanvas();
    void bindHandler();
    void afterProjectChange(bool fitView = true);  // rebind, fit the view, remember it
    void render();
    void setStatus(const std::string& text, bool error = false);
    void handle(InputResult result);

    void showContextMenu();
    void showProjectMenu(int x, int y, bool onCanvas = false);
    void showNodeMenu(int x, int y);
    void showConnectionMenu(int x, int y);
    void showStartPage();

    void openProjectDialog(int x, int y);
    void recentDialog(int x, int y);
    void openPathDialog();
    void importDialog();
    void exportDialog(int x, int y);
    void saveAsDialog();
    void renameDialog();
    void switchProjectDialog(int x, int y);
    void languageDialog(int x, int y);
    bool openPath(const std::string& path);
};

}  // namespace ic

#endif  // IC_APP_H
