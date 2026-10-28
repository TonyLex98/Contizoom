#include "app/demo.h"
#include "app/settings.h"
#include "i18n/i18n.h"
#include "model/canvas.h"
#include "storage/exchange.h"
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
               "  infinite_canvas                      reopen the last project (the welcome project at first)\n"
               "  infinite_canvas <project.canvas>     open a project\n"
               "  infinite_canvas -n <name>            start a new project\n"
               "  infinite_canvas --import <file>      import .md / .opml / .json / .txt as a new project\n"
               "  infinite_canvas --export <out> <project.canvas>\n"
               "                                       export, format by extension: .md .opml .json .txt\n"
               "  infinite_canvas --snapshot <out.txt> [project.canvas] [--size WxH]\n"
               "                                       render a project (default: the demo) to a text file\n"
               "  infinite_canvas --version | --help\n"
               "\n"
               "Options:\n"
               "  --lang en|ru|zh|fr|es                interface language (default: saved choice, then system)\n"
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

int exportProject(const std::string& out, const std::string& project) {
    if (project.empty()) {
        error("--export needs a project to export");
        return 2;
    }
    const auto format = ic::exchange::detect(out, "");
    if (!format) {
        error("unknown export format for " + out + " (use .md, .opml, .json or .txt)");
        return 2;
    }
    ic::Canvas canvas;
    ic::JsonStorage storage;
    if (!storage.loadCanvas(canvas, project)) {
        error(storage.lastError());
        return 1;
    }
    if (!ic::file::write(ic::path::fromUtf8(out), ic::exchange::exportAs(canvas, *format))) {
        error("cannot write " + out);
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string projectPath, newName, snapshotOut, exportOut, importPath, langArg;
    int snapW = 120, snapH = 40;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--lang" && i + 1 < argc) {
            langArg = argv[++i];
            continue;
        }
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
        } else if (arg == "--export" && i + 1 < argc) {
            exportOut = argv[++i];
        } else if (arg == "--import" && i + 1 < argc) {
            importPath = argv[++i];
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

    // Interface language: --lang, else the saved choice, else the system's.
    if (!langArg.empty()) {
        const auto lang = ic::i18n::parse(langArg);
        if (!lang) {
            error("unknown language " + langArg + " (use en, ru, zh, fr or es)");
            return 2;
        }
        ic::i18n::setLanguage(*lang);
    } else if (auto saved = ic::i18n::parse(ic::Settings::load().language)) {
        ic::i18n::setLanguage(*saved);
    } else {
        ic::i18n::setLanguage(ic::i18n::detectSystemLanguage());
    }

    if (!snapshotOut.empty()) return snapshot(snapshotOut, projectPath, snapW, snapH);
    if (!exportOut.empty()) return exportProject(exportOut, projectPath);

#ifdef _WIN32
    ic::App app;
    if (!newName.empty()) {
        app.projects().createProject(newName);
    } else if (!importPath.empty()) {
        if (app.projects().importProject(importPath).empty()) {
            error(app.projects().lastError());
            return 1;
        }
    } else if (!projectPath.empty()) {
        if (app.projects().openProject(projectPath).empty()) {
            error(app.projects().lastError());
            return 1;
        }
    } else {
        app.openStartupProject();
    }
    return app.run();
#else
    std::fputs("The interactive editor needs the Windows console; only --snapshot is available here.\n", stderr);
    return 2;
#endif
}
