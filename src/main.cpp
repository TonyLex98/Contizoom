#include "app/demo.h"
#include "model/canvas.h"
#include "storage/json_storage.h"
#include "util/file.h"
#include "util/path.h"
#include "version/version.h"

#ifdef _WIN32
#include "app/app.h"
#endif

#include <cstdio>
#include <string>

namespace {

void printUsage() {
    std::fputs("Infinite Canvas " IC_VERSION_STRING " - zoomable outliner for the terminal\n"
               "\n"
               "Usage:\n"
               "  infinite_canvas                      open the welcome project\n"
               "  infinite_canvas <project.canvas>     open a project\n"
               "  infinite_canvas -n <name>            start a new project\n"
               "  infinite_canvas --snapshot <out.txt> [project.canvas] [--size WxH]\n"
               "                                       render a project (default: the demo) to a text file\n"
               "  infinite_canvas --version | --help\n"
               "\n"
               "Project format " IC_STORAGE_FORMAT_STRING "\n",
               stdout);
}

void error(const std::string& message) {
    std::fprintf(stderr, "error: %s\n", message.c_str());
}

int snapshot(const std::string& out, const std::string& project, int w, int h) {
    ic::Canvas canvas;
    if (project.empty()) {
        ic::buildDemoCanvas(canvas);
    } else {
        ic::JsonStorage storage;
        if (!storage.loadCanvas(canvas, project)) {
            error(storage.lastError());
            return 1;
        }
    }
    if (!ic::file::write(ic::path::fromUtf8(out), ic::renderSnapshot(canvas, w, h))) {
        error("cannot write " + out);
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string projectPath, newName, snapshotOut;
    int snapW = 120, snapH = 40;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            std::puts(IC_VERSION_STRING);
            return 0;
        }
        if (arg == "-n" && i + 1 < argc) {
            newName = argv[++i];
        } else if (arg == "--snapshot" && i + 1 < argc) {
            snapshotOut = argv[++i];
        } else if (arg == "--size" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &snapW, &snapH) != 2 || snapW < 40 || snapH < 10) {
                error("--size expects WxH, at least 40x10");
                return 2;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            error("unknown option " + arg);
            printUsage();
            return 2;
        } else {
            projectPath = arg;
        }
    }

    if (!snapshotOut.empty()) return snapshot(snapshotOut, projectPath, snapW, snapH);

#ifdef _WIN32
    ic::App app;
    if (!newName.empty()) {
        app.projects().createProject(newName);
    } else if (!projectPath.empty()) {
        if (app.projects().openProject(projectPath).empty()) {
            error(app.projects().lastError());
            return 1;
        }
    } else {
        app.openDemo();
    }
    return app.run();
#else
    std::fputs("The interactive editor needs the Windows console; only --snapshot is available here.\n", stderr);
    return 2;
#endif
}
