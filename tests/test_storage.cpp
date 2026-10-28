#include "test.h"
#include "model/canvas.h"
#include "storage/json_storage.h"
#include "util/clock.h"
#include "util/path.h"

#include <filesystem>
#include <fstream>

using namespace ic;
namespace fs = std::filesystem;

namespace {

// A fresh directory under the system temp dir, removed afterwards.
struct TempDir {
    fs::path dir;
    TempDir() {
        static int counter = 0;
        dir = fs::temp_directory_path() /
              ("ic_test_" + std::to_string(clock::monotonicMillis()) + "_" + std::to_string(counter++));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string project(const std::string& name) const { return path::toUtf8(dir / path::fromUtf8(name)); }
};

void writeText(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

}  // namespace

TEST(storage_round_trips_a_project) {
    TempDir tmp;
    Canvas c;
    c.canvasName = "Проект";
    c.viewport = Viewport{12.25, -7.5, 0.37};
    const std::string root = c.createNode("Line 1\nLine \"2\"", {1234567.125, 0.1, 40, 12}, "Корень");
    const std::string child = c.createNode("", {3, 4, 10, 5}, "child", root);
    const std::string other = c.createNode("", {300, 0, 20, 5}, "other");
    const std::string conn = c.createConnection(child, other, "uses", ConnType::DashedArrow);
    c.getConnection(conn)->fromAnchor = {AnchorEdge::Right, 2};

    JsonStorage storage;
    const std::string path = tmp.project("Проект.canvas");
    CHECK(storage.saveCanvas(c, path));
    CHECK(storage.isValid(path));

    Canvas back;
    CHECK(storage.loadCanvas(back, path));
    CHECK_EQ(back.canvasName, c.canvasName);
    CHECK_EQ(back.allNodes().size(), static_cast<size_t>(3));
    CHECK_NEAR(back.viewport.scale, 0.37, 1e-15);

    auto r = back.getNode(root);
    CHECK(r != nullptr);
    CHECK_EQ(r->name, std::string("Корень"));
    CHECK_EQ(r->text, std::string("Line 1\nLine \"2\""));
    CHECK_EQ(r->rect.x, 1234567.125);  // exact, no 6-digit rounding
    CHECK_EQ(back.getNode(child)->parentId, root);
    CHECK_EQ(back.getNode(child)->depth, 1);

    auto bc = back.getConnection(conn);
    CHECK(bc != nullptr);
    CHECK_EQ(bc->name, std::string("uses"));
    CHECK(bc->type == ConnType::DashedArrow);
    CHECK(bc->fromAnchor == (Anchor{AnchorEdge::Right, 2}));
    CHECK(!bc->toAnchor.valid());
}

TEST(storage_save_removes_files_of_deleted_nodes) {
    TempDir tmp;
    Canvas c;
    const std::string a = c.createNode("", {0, 0, 10, 10}, "a");
    const std::string b = c.createNode("", {20, 0, 10, 10}, "b");
    JsonStorage storage;
    const std::string path = tmp.project("p.canvas");
    CHECK(storage.saveCanvas(c, path));
    c.removeSubtree(b);
    CHECK(storage.saveCanvas(c, path));
    CHECK(fs::exists(tmp.dir / "p.canvas" / "nodes" / (a + ".json")));
    CHECK(!fs::exists(tmp.dir / "p.canvas" / "nodes" / (b + ".json")));
}

TEST(storage_reads_legacy_1_0_projects) {
    TempDir tmp;
    const fs::path p = tmp.dir / "old.canvas";
    // Written by the original version: no "format", zoomLevel fields.
    writeText(p / "config.json", R"({
  "canvas_name": "Old",
  "created_at": 1700000000,
  "modified_at": 1700000001,
  "viewport": { "centerX": 5, "centerY": 6, "zoomLevel": 2, "textScale": 1.5 },
  "connections": [ { "uuid": "c1", "name": "", "fromUuid": "A", "toUuid": "B", "type": 0 } ]
})");
    writeText(p / "nodes" / "A.json", R"({"uuid": "A", "name": "a", "text": "", "parentId": "", "x": 0, "y": 0,
        "width": 20, "height": 5, "zoomLevel": 0, "tag": "", "hasFrame": false, "createdAt": 1, "modifiedAt": 2})");
    writeText(p / "nodes" / "B.json", R"({"uuid": "B", "name": "b", "text": "", "parentId": "A", "x": 1, "y": 1,
        "width": 5, "height": 2, "zoomLevel": 7})");

    Canvas c;
    JsonStorage storage;
    CHECK(storage.loadCanvas(c, path::toUtf8(p)));
    CHECK_EQ(c.canvasName, std::string("Old"));
    CHECK_NEAR(c.viewport.scale, 1.5, 1e-12);
    CHECK_EQ(c.getNode("B")->depth, 1);  // recomputed, stored zoomLevel ignored
    CHECK(c.getConnection("c1") != nullptr);
    CHECK(!c.getConnection("c1")->fromAnchor.valid());  // no anchor fields
}

TEST(storage_rejects_newer_major_format_without_touching_canvas) {
    TempDir tmp;
    const fs::path p = tmp.dir / "future.canvas";
    writeText(p / "config.json", R"({"format": "2.0", "canvas_name": "Future"})");

    Canvas c;
    c.canvasName = "keep me";
    c.createNode("", {0, 0, 10, 10}, "existing");
    JsonStorage storage;
    CHECK(!storage.loadCanvas(c, path::toUtf8(p)));
    CHECK(storage.lastError().find("unsupported") != std::string::npos);
    CHECK_EQ(c.canvasName, std::string("keep me"));
    CHECK_EQ(c.allNodes().size(), static_cast<size_t>(1));
}

TEST(storage_skips_damaged_node_files_and_dangling_connections) {
    TempDir tmp;
    const fs::path p = tmp.dir / "damaged.canvas";
    writeText(p / "config.json",
              R"({"format": "1.1", "connections": [{"uuid": "c", "fromUuid": "A", "toUuid": "BROKEN"}]})");
    writeText(p / "nodes" / "A.json", R"({"uuid": "A", "x": 0, "y": 0, "width": 10, "height": 5})");
    writeText(p / "nodes" / "BROKEN.json", R"({"uuid": "BROKEN", "x": )");

    Canvas c;
    JsonStorage storage;
    CHECK(storage.loadCanvas(c, path::toUtf8(p)));
    CHECK_EQ(storage.skippedNodeFiles(), 1);
    CHECK_EQ(c.allNodes().size(), static_cast<size_t>(1));
    CHECK(c.allConnections().empty());
}

TEST(storage_reports_unreadable_config) {
    TempDir tmp;
    writeText(tmp.dir / "bad.canvas" / "config.json", "{ not json");
    Canvas c;
    JsonStorage storage;
    CHECK(!storage.loadCanvas(c, tmp.project("bad.canvas")));
    CHECK(!storage.lastError().empty());
    CHECK(!storage.isValid(tmp.project("missing.canvas")));
}
