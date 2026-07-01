#include "app/app.h"
#include "app/project_manager.h"
#include "model/canvas.h"
#include "version/version.h"
#include <iostream>

using namespace ic;

int main(int argc, char* argv[]) {
    App app;

    std::string canvasPath;
    std::string canvasName = "Untitled";
    bool isNew = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Infinite Canvas v" << IC_VERSION_STRING
                      << " | Format " << IC_STORAGE_FORMAT_STRING << "\n";
            std::cout << "Usage: infinite_canvas [path] [-n name]\n";
            return 0;
        }
        if (arg == "-n" && i + 1 < argc) {
            isNew = true; canvasName = argv[++i];
        }
        else if (canvasPath.empty()) {
            canvasPath = arg;
        }
    }

    app.initialize(120, 40);

    if (isNew) {
        app.createProject(canvasName);
    }
    else if (!canvasPath.empty()) {
        if (app.openProject(canvasPath).empty()) {
            std::cerr << "Failed to load: " << canvasPath << "\n";
            return 1;
        }
    }
    else {
        // Demo: fractal layout — children INSIDE parents
        app.createProject("Fractal Demo");
        auto& c = *app.canvas();
        c.viewport_.centerX = 60;
        c.viewport_.centerY = 15;
        c.viewport_.textScale = 1.0;

        // Level 0: root (absolute world coords)
        std::string root = c.createNode(
            "Project Root\nMain architecture", Rect{15, 3, 90, 24}, "", 0);

        // Level 1: children of root (relative inside parent: x 0..90, y 0..24)
        std::string d1 = c.createNode("Design",   Rect{2, 2, 42, 10}, root, 1);
        std::string d2 = c.createNode("Code",     Rect{46, 2, 42, 10}, root, 1);
        std::string d3 = c.createNode("Tests",    Rect{2, 14, 42, 8}, root, 1);
        std::string d4 = c.createNode("Docs",     Rect{46, 14, 42, 8}, root, 1);

        // Level 2: children inside level-1 (relative inside parent)
        c.createNode("UI spec",   Rect{2, 2, 18, 3}, d1, 2);
        c.createNode("API spec",  Rect{22, 2, 18, 3}, d1, 2);
        c.createNode("main.cpp",  Rect{2, 2, 18, 3}, d2, 2);
        c.createNode("utils.cpp", Rect{22, 2, 18, 3}, d2, 2);
        c.createNode("unit",      Rect{2, 2, 18, 3}, d3, 2);
        c.createNode("integr",    Rect{22, 2, 18, 3}, d3, 2);

        // Level 3: children inside Design
        c.createNode("pages", Rect{4, 4, 8, 2}, d1, 3);
        c.createNode("ctrl",  Rect{14, 4, 8, 2}, d1, 3);

        // Connections
        c.createConnection(root, d1, ConnType::Arrow);
        c.createConnection(root, d2, ConnType::Arrow);
        c.createConnection(root, d3, ConnType::DottedArrow);
        c.createConnection(root, d4, ConnType::DottedArrow);
        c.createConnection(d1, d2, ConnType::DashedLine);

        // Auto-layout: expand parents and resolve overlaps
        c.autoLayout();
    }

    std::cout << "Right-click to open Project Menu\n";
    std::cout << "Projects directory: " << ProjectManager::defaultProjectsDir() << "\n";

    app.run();
    return 0;
}
