#include "test.h"
#include "app/project_manager.h"
#include "model/canvas.h"
#include "storage/json_storage.h"
#include "storage/markdown_export.h"
#include "util/clock.h"
#include "util/path.h"
#include "util/process.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace ic;
namespace fs = std::filesystem;

namespace {

// Points the projects folder at a private temp directory for the test.
struct TempHome {
    fs::path dir;
    std::string saved;
    const char* var;
    TempHome() {
#ifdef _WIN32
        var = "USERPROFILE";
#else
        var = "HOME";
#endif
        if (const char* v = std::getenv(var)) saved = v;
        static int counter = 0;
        dir = fs::temp_directory_path() /
              ("ic_home_" + std::to_string(clock::monotonicMillis()) + "_" + std::to_string(counter++));
        fs::create_directories(dir);
        setEnv(path::toUtf8(dir));
    }
    ~TempHome() {
        setEnv(saved);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void setEnv(const std::string& value) {
#ifdef _WIN32
        _putenv_s(var, value.c_str());
#else
        setenv(var, value.c_str(), 1);
#endif
    }
};

}  // namespace

TEST(project_new_project_is_written_on_first_edit) {
    TempHome home;
    ProjectManager pm(std::make_unique<JsonStorage>());
    pm.createProject("Мой проект");
    const std::string path = pm.activeProject()->path;
    CHECK(path.find("Мой_проект.canvas") != std::string::npos);  // non-ASCII names survive

    pm.flushAll();
    CHECK(!fs::exists(path::fromUtf8(path)));  // nothing to save yet: no empty folders

    pm.activeCanvas()->createNode("", {0, 0, 10, 5}, "first");
    pm.flushAll();
    JsonStorage storage;
    CHECK(storage.isValid(path));
    CHECK(pm.activeProject()->onDisk);
}

TEST(project_suggested_paths_never_collide) {
    TempHome home;
    ProjectManager pm(std::make_unique<JsonStorage>());
    pm.createProject("Same");
    pm.createProject("Same");  // neither is on disk yet
    CHECK(pm.projects()[0].path != pm.projects()[1].path);
}

TEST(project_auto_save_writes_only_what_changed) {
    TempHome home;
    ProjectManager pm(std::make_unique<JsonStorage>());
    pm.createProject("Inc");
    Canvas& c = *pm.activeCanvas();
    const std::string a = c.createNode("", {0, 0, 10, 5}, "a");
    const std::string b = c.createNode("", {20, 0, 10, 5}, "b");
    pm.flushAll();
    const fs::path dir = path::fromUtf8(pm.activeProject()->path);

    // Remove b's file behind the app's back: a camera move must not
    // rewrite it (config only), an edit of `a` must not either.
    fs::remove(dir / "nodes" / (b + ".json"));
    c.viewport.centerX = 42;
    c.markConfigChanged();
    c.getNode(a)->text = "changed";
    c.markChanged(a);
    pm.flushAll();
    CHECK(!fs::exists(dir / "nodes" / (b + ".json")));

    Canvas reloaded;
    JsonStorage storage;
    CHECK(storage.loadCanvas(reloaded, pm.activeProject()->path));
    CHECK_EQ(reloaded.getNode(a)->text, std::string("changed"));
    CHECK_NEAR(reloaded.viewport.centerX, 42.0, 1e-12);

    // Deleting removes the file.
    c.removeSubtree(a);
    pm.flushAll();
    CHECK(!fs::exists(dir / "nodes" / (a + ".json")));
}

TEST(project_reopen_round_trip_and_summary) {
    TempHome home;
    std::string path;
    {
        ProjectManager pm(std::make_unique<JsonStorage>());
        pm.createProject("Notes");
        pm.activeCanvas()->createNode("", {0, 0, 10, 5}, "x");
        pm.activeCanvas()->createNode("", {20, 0, 10, 5}, "y");
        CHECK(pm.saveCurrent());
        path = pm.activeProject()->path;
    }
    ProjectManager pm(std::make_unique<JsonStorage>());
    const auto list = pm.listSavedProjects();
    CHECK_EQ(list.size(), static_cast<size_t>(1));
    CHECK_EQ(list[0].name, std::string("Notes"));
    CHECK_EQ(list[0].nodeCount, 2);
    CHECK(!pm.openProject(path).empty());
    CHECK_EQ(pm.activeCanvas()->allNodes().size(), static_cast<size_t>(2));
    CHECK_EQ(pm.openProject(path), pm.activeProject()->uuid);  // opening twice just switches
    CHECK_EQ(pm.projects().size(), static_cast<size_t>(1));
}

TEST(project_save_as_continues_in_the_copy) {
    TempHome home;
    ProjectManager pm(std::make_unique<JsonStorage>());
    pm.createProject("Original");
    pm.activeCanvas()->createNode("", {0, 0, 10, 5}, "a");
    CHECK(pm.saveCurrent());
    const std::string original = pm.activeProject()->path;

    CHECK(pm.saveAs("Copy"));
    const std::string copy = pm.activeProject()->path;
    CHECK(copy != original);
    pm.activeCanvas()->createNode("", {20, 0, 10, 5}, "only in copy");
    pm.flushAll();

    Canvas o, c;
    JsonStorage storage;
    CHECK(storage.loadCanvas(o, original));
    CHECK(storage.loadCanvas(c, copy));
    CHECK_EQ(o.allNodes().size(), static_cast<size_t>(1));
    CHECK_EQ(c.allNodes().size(), static_cast<size_t>(2));
    CHECK_EQ(c.canvasName, std::string("Copy"));
}

TEST(project_revert_discards_unsaved_edits) {
    TempHome home;
    ProjectManager pm(std::make_unique<JsonStorage>());
    pm.createProject("R");
    const std::string a = pm.activeCanvas()->createNode("", {0, 0, 10, 5}, "a");
    CHECK(pm.saveCurrent());
    pm.activeCanvas()->getNode(a)->name = "edited";
    pm.activeCanvas()->markChanged(a);  // still pending (throttle)
    CHECK(pm.revert());
    CHECK_EQ(pm.activeCanvas()->getNode(a)->name, std::string("a"));
}

TEST(project_lock_warns_about_another_live_window) {
    TempHome home;
    std::string path;
    {
        ProjectManager pm(std::make_unique<JsonStorage>());
        pm.createProject("Locked");
        pm.activeCanvas()->createNode("", {0, 0, 10, 5}, "a");
        CHECK(pm.saveCurrent());
        path = pm.activeProject()->path;
        CHECK(fs::exists(path::fromUtf8(path) / ".lock"));
    }
    CHECK(!fs::exists(path::fromUtf8(path) / ".lock"));  // released on close

    // A live process holding it: warn.
    std::ofstream(path::fromUtf8(path) / ".lock") << process::currentId() << " other-session\n";
    {
        ProjectManager pm(std::make_unique<JsonStorage>());
        CHECK(!pm.openProject(path).empty());
        CHECK(pm.takeWarning().find("another window") != std::string::npos);
    }
    // A stale lock from a crashed process: no warning.
    std::ofstream(path::fromUtf8(path) / ".lock") << 0 << " crashed-session\n";
    {
        ProjectManager pm(std::make_unique<JsonStorage>());
        CHECK(!pm.openProject(path).empty());
        CHECK(pm.takeWarning().empty());
    }
}

TEST(markdown_export_outlines_the_canvas) {
    Canvas c;
    c.canvasName = "Plan";
    const std::string a = c.createNode("Line one\nLine two", {0, 0, 40, 20}, "Goals");
    const std::string b = c.createNode("", {2, 5, 10, 5}, "Ship it", a);
    const std::string d = c.createNode("", {60, 0, 10, 5}, "Done");
    const std::string conn = c.createConnection(b, d, "leads to");
    (void)conn;
    const std::string md = exportMarkdown(c);
    CHECK(md.find("# Plan") == 0);
    CHECK(md.find("## Goals") != std::string::npos);
    CHECK(md.find("Line two") != std::string::npos);
    CHECK(md.find("### Ship it") != std::string::npos);
    CHECK(md.find("Ship it → Done: *leads to*") != std::string::npos);
    CHECK(md.find("## Goals") < md.find("## Done"));  // reading order
}

TEST(project_unsupported_and_missing_paths_fail_cleanly) {
    TempHome home;
    ProjectManager pm(std::make_unique<JsonStorage>());
    CHECK(pm.openProject(path::toUtf8(home.dir / "nope.canvas")).empty());
    CHECK(!pm.lastError().empty());
    CHECK(pm.activeProject() == nullptr);
}
