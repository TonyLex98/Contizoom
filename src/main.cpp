#include "app/app.h"
#include "app/project_manager.h"
#include "model/canvas.h"
#include "version/version.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;

using namespace ic;

static void createDemoFiles(const std::string& base) {
    auto mk = [](const std::string& p) { fs::create_directories(p); };
    auto wf = [](const std::string& p, const std::string& c) {
        std::ofstream(p).write(c.data(), static_cast<std::streamsize>(c.size()));
    };

    mk(base + "/Design");
    mk(base + "/Code");
    mk(base + "/Tests/unit");
    mk(base + "/Tests/integr");
    mk(base + "/Docs");

    wf(base + "/README.md", "## Project Root\n\nMain architecture documentation.");
    wf(base + "/Design/README.md", "# Design\nDesign documentation.");
    wf(base + "/Design/UI_spec.md", "# UI Spec\nUser interface specification.");
    wf(base + "/Design/API_spec.md", "# API Spec\nAPI specification.");
    wf(base + "/Code/main.cpp", "#include <iostream>\nint main() { return 0; }");
    wf(base + "/Code/utils.cpp", "#include \"utils.h\"\n// Utility functions");
    wf(base + "/Tests/unit/README.md", "# Unit Tests");
    wf(base + "/Tests/integr/README.md", "# Integration Tests");
    wf(base + "/Docs/README.md", "# Docs\nDocumentation");
}

static std::string basenameStr(const fs::path& p) {
    std::string bn = p.has_filename() ? p.filename().u8string() : "";
    if (!bn.empty()) {
        auto dot = bn.find('.');
        if (dot != std::string::npos) bn.erase(dot);
    }
    return bn.empty() ? "item" : bn;
}

static std::string scanFirstLines(const fs::path& p, int maxLines = 3) {
    std::string res;
    if (fs::is_regular_file(p)) {
        std::ifstream ifs(p);
        std::string line;
        int n = 0;
        while (std::getline(ifs, line) && n < maxLines) {
            if (!res.empty()) res += "\n";
            if (static_cast<int>(res.size()) + static_cast<int>(line.size()) > 200) break;
            res += line; n++;
        }
    }
    return res.empty() ? std::string(p.u8string()) : res;
}

static void scanDirIntoCanvas(Canvas& c, const fs::path& dirPath, const std::string& parentUuid, int level) {
    std::vector<std::pair<std::string, bool>> entries;
    try {
        for (const auto& e : fs::directory_iterator(dirPath)) {
            entries.emplace_back(e.path().filename().u8string(), e.is_directory());
        }
    } catch (...) { return; }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second;
        return a.first < b.first;
    });

    for (const auto& [name, isDir] : entries) {
        auto fp = dirPath / name;
        std::string bn = basenameStr(fp);
        std::string display = isDir ? bn : name;
        std::string text = isDir ? std::string("{") + std::to_string(entries.size()) + std::string(" items") : scanFirstLines(fp);

        Rect r{1, 1, 15, 3};
        if (!isDir && text.size() > 20) r.height = 3;
        std::string uuid = c.createNode(text, r, display, parentUuid, level);

        if (isDir) {
            try { scanDirIntoCanvas(c, fp, uuid, level + 1); } catch (...) {}
        }
    }
}

static void loadDemoFromDir(Canvas& c, const std::string& demoDir) {
    if (!fs::exists(demoDir) || !fs::is_directory(demoDir)) {
        createDemoFiles(demoDir);
    }

    c.canvasName = fs::path(demoDir).filename().u8string();
    c.viewport_.centerX = 60;
    c.viewport_.centerY = 15;
    c.viewport_.textScale = 1.0;

    std::string root = c.createNode(
        fs::path(demoDir).filename().u8string() + "\nFile system view",
        Rect{5, 2, 100, 30}, fs::path(demoDir).filename().u8string(), "", 0);

    try { scanDirIntoCanvas(c, demoDir, root, 1); } catch (...) {}
    c.autoLayout();
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
        // Demo: load from saved file or create from demo_project/
        fs::path exeDir = fs::current_path();
        std::string demoDir = (exeDir / "demo_project").u8string();
        std::string savedPath = (exeDir / "demo.canvas").u8string();

        if (fs::exists(savedPath + "/manifest.json")) {
            // Load saved project
            if (app.openProject(savedPath).empty()) {
                std::cerr << "Failed to load saved demo, recreating...\n";
                app.createProject("File System Demo");
                loadDemoFromDir(*app.canvas(), demoDir);
            }
        } else {
            // First run: create from files and save
            app.createProject("File System Demo");
            loadDemoFromDir(*app.canvas(), demoDir);
            app.saveAs(savedPath);
        }
    }

    std::cout << "Right-click to open Project Menu\n";
    std::cout << "Projects directory: " << ProjectManager::defaultProjectsDir() << "\n";

    app.run();

    return 0;
}
