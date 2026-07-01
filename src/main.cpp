#include "app/app.h"
#include "app/project_manager.h"
#include "model/canvas.h"
#include "version/version.h"
#include <iostream>
#include <filesystem>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs = std::filesystem;

using namespace ic;

// Directory the running executable actually lives in. Unlike
// fs::current_path(), this doesn't change with the caller's working
// directory, so the demo project/canvas always resolve to the same place
// instead of spawning a fresh copy every time the exe is launched from
// a different folder.
static fs::path getExecutableDir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return fs::current_path();
    return fs::path(buf).parent_path();
}

static const char* kDemoRootName = "demo_project";

static std::string addDemoFolder(Canvas& c, const std::string& parentUuid, int level,
                                  const std::string& name, int childCount) {
    std::string text = "{" + std::to_string(childCount) + " items";
    return c.createNode(text, Rect{1, 1, 15, 3}, name, parentUuid, level);
}

static void addDemoFile(Canvas& c, const std::string& parentUuid, int level,
                         const std::string& name, const std::string& text) {
    c.createNode(text, Rect{1, 1, 15, 3}, name, parentUuid, level);
}

// Builds the sample "file system view" project directly as canvas nodes.
// Nothing touches disk here - the demo used to write real .md files and
// scan them back in just to populate this same tree, which meant every
// launch from a new working directory left stray demo_project/ folders
// lying around. The canvas itself is the only storage that matters; it's
// persisted as JSON via app.saveAs() right after this runs.
static void buildDemoCanvas(Canvas& c) {
    c.clear();
    c.canvasName = kDemoRootName;
    c.viewport_.centerX = 60;
    c.viewport_.centerY = 15;
    c.viewport_.textScale = 1.0;

    std::string root = c.createNode(std::string(kDemoRootName) + "\nFile system view",
                                     Rect{5, 2, 100, 30}, kDemoRootName, "", 0);

    std::string code = addDemoFolder(c, root, 1, "Code", 2);
    addDemoFile(c, code, 2, "main.cpp", "#include <iostream>\nint main() { return 0; }");
    addDemoFile(c, code, 2, "utils.cpp", "#include \"utils.h\"\n// Utility functions");

    std::string design = addDemoFolder(c, root, 1, "Design", 3);
    addDemoFile(c, design, 2, "API_spec.md", "# API Spec\nAPI specification.");
    addDemoFile(c, design, 2, "overview.md", "# Design\nDesign documentation.");
    addDemoFile(c, design, 2, "UI_spec.md", "# UI Spec\nUser interface specification.");

    std::string docs = addDemoFolder(c, root, 1, "Docs", 1);
    addDemoFile(c, docs, 2, "guide.md", "# Docs\nDocumentation");

    std::string tests = addDemoFolder(c, root, 1, "Tests", 2);
    std::string integr = addDemoFolder(c, tests, 2, "integr", 1);
    addDemoFile(c, integr, 3, "scenarios.md", "# Integration Tests");
    std::string unit = addDemoFolder(c, tests, 2, "unit", 1);
    addDemoFile(c, unit, 3, "coverage.md", "# Unit Tests");

    // Only one README.md in the whole demo - the real project root, matching
    // how actual projects are laid out. Every other file has its own name so
    // the canvas doesn't show five identically-labeled boxes.
    addDemoFile(c, root, 1, "README.md", "## Project Root\n\nMain architecture documentation.");

    c.autoLayout();
}

static bool demoHasDuplicateRoots(const Canvas& c) {
    int matchingRoots = 0;

    for (const auto& [_, node] : c.allNodes()) {
        if (!node->parentId.empty()) continue;
        if (node->name == kDemoRootName || node->text.find("File system view") != std::string::npos) {
            matchingRoots++;
        }
    }

    return matchingRoots > 1;
}

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
        // Demo: load the previously saved JSON canvas, or build it once in memory.
        fs::path exeDir = getExecutableDir();
        std::string savedPath = (exeDir / "demo.canvas").u8string();

        if (fs::exists(savedPath + "/config.json")) {
            // Load saved project
            if (app.openProject(savedPath).empty()) {
                std::cerr << "Failed to load saved demo, recreating...\n";
                app.createProject("File System Demo");
                buildDemoCanvas(*app.canvas());
                app.saveAs(savedPath);
            } else if (app.canvas() && demoHasDuplicateRoots(*app.canvas())) {
                app.canvas()->clear();
                buildDemoCanvas(*app.canvas());
                app.saveAs(savedPath);
            }
        } else {
            // First run: build the demo canvas and save it as JSON
            app.createProject("File System Demo");
            buildDemoCanvas(*app.canvas());
            app.saveAs(savedPath);
        }
    }

    std::cout << "Right-click to open Project Menu\n";
    std::cout << "Projects directory: " << ProjectManager::defaultProjectsDir() << "\n";

    app.run();

    return 0;
}
