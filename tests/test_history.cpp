#include "test.h"
#include "input/history.h"
#include "model/canvas.h"

using namespace ic;

TEST(history_undo_and_redo_restore_structure) {
    Canvas c;
    History h;
    const std::string root = c.createNode("", {0, 0, 40, 20}, "root");

    std::string before = h.snapshot(c);
    const std::string child = c.createNode("text", {2, 2, 10, 5}, "child", root);
    CHECK(h.record(before, c));

    before = h.snapshot(c);
    c.removeSubtree(root);
    CHECK(h.record(before, c));
    CHECK(c.allNodes().empty());

    CHECK(h.undo(c));  // the delete
    CHECK_EQ(c.allNodes().size(), static_cast<size_t>(2));
    CHECK_EQ(c.getNode(child)->parentId, root);
    CHECK_EQ(c.getNode(child)->depth, 1);
    CHECK_EQ(c.getNode(child)->text, std::string("text"));

    CHECK(h.undo(c));  // the create
    CHECK(!c.getNode(child));
    CHECK(!h.canUndo());

    CHECK(h.redo(c));
    CHECK(c.getNode(child) != nullptr);
    CHECK(h.redo(c));
    CHECK(c.allNodes().empty());
    CHECK(!h.canRedo());
}

TEST(history_ignores_actions_that_change_nothing) {
    Canvas c;
    History h;
    c.createNode("", {0, 0, 10, 10}, "a");
    const std::string before = h.snapshot(c);
    c.viewport.centerX = 500;  // camera moves are not undo steps
    CHECK(!h.record(before, c));
    CHECK(!h.canUndo());
}

TEST(history_new_action_clears_redo_and_respects_limit) {
    Canvas c;
    History h(3);
    for (int i = 0; i < 5; ++i) {
        const std::string before = h.snapshot(c);
        c.createNode("", {i * 20.0, 0, 10, 10}, "n" + std::to_string(i));
        h.record(before, c);
    }
    CHECK_EQ(h.undoDepth(), static_cast<size_t>(3));
    h.undo(c);
    CHECK(h.canRedo());
    const std::string before = h.snapshot(c);
    c.createNode("", {0, 50, 10, 10}, "fresh");
    h.record(before, c);
    CHECK(!h.canRedo());
}

TEST(history_restore_marks_everything_for_saving) {
    Canvas c;
    History h;
    const std::string before = h.snapshot(c);
    c.createNode("", {0, 0, 10, 10}, "a");
    h.record(before, c);
    c.discardPendingChanges();

    Canvas::ChangeSet seen;
    c.onChanged([&](const Canvas::ChangeSet& s) { seen = s; });
    h.undo(c);
    c.flushChanges();
    CHECK(seen.full);
}
