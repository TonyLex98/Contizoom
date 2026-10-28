#include "test.h"
#include "model/canvas.h"

using namespace ic;

namespace {

// root(0,0) > a(10,10) > b(5,5);  root > c(60,10)
struct Tree {
    Canvas c;
    std::string root, a, b, sib;
    Tree() {
        root = c.createNode("", {0, 0, 100, 60}, "root");
        a = c.createNode("", {10, 10, 30, 20}, "a", root);
        b = c.createNode("", {5, 5, 10, 6}, "b", a);
        sib = c.createNode("", {60, 10, 30, 20}, "sib", root);
    }
};

}  // namespace

TEST(canvas_create_node_tracks_parent_and_depth) {
    Tree t;
    CHECK_EQ(t.c.getNode(t.b)->depth, 2);
    CHECK_EQ(t.c.getNode(t.a)->childrenUuids.size(), static_cast<size_t>(1));
    CHECK(t.c.createNode("", {0, 0, 10, 10}, "x", "no-such-parent").empty());
}

TEST(canvas_world_rect_sums_parent_offsets) {
    Tree t;
    const Rect w = t.c.computeWorldRect(t.b);
    CHECK_NEAR(w.x, 15.0, 1e-9);
    CHECK_NEAR(w.y, 15.0, 1e-9);
}

TEST(canvas_reparent_refuses_cycles) {
    Tree t;
    CHECK(!t.c.reparent(t.a, t.b));    // into its own child
    CHECK(!t.c.reparent(t.a, t.a));    // into itself
    CHECK(!t.c.reparent(t.root, t.b)); // into a grandchild
    CHECK_EQ(t.c.getNode(t.a)->parentId, t.root);
}

TEST(canvas_reparent_keeps_world_position_and_updates_depth) {
    Tree t;
    const Rect before = t.c.computeWorldRect(t.b);
    CHECK(t.c.reparent(t.b, t.sib));
    const Rect after = t.c.computeWorldRect(t.b);
    CHECK_NEAR(after.x, before.x, 1e-9);
    CHECK_NEAR(after.y, before.y, 1e-9);
    CHECK_EQ(t.c.getNode(t.b)->parentId, t.sib);
    CHECK(t.c.getNode(t.a)->childrenUuids.empty());

    CHECK(t.c.reparent(t.a, ""));  // to root: subtree depth follows
    CHECK_EQ(t.c.getNode(t.a)->depth, 0);
}

TEST(canvas_promote_lifts_next_to_former_parent) {
    Tree t;
    CHECK(t.c.promoteNode(t.b));
    auto b = t.c.getNode(t.b);
    auto a = t.c.getNode(t.a);
    CHECK_EQ(b->parentId, t.root);
    CHECK_EQ(b->depth, 1);
    CHECK(b->rect.x >= a->rect.right());  // beside the old parent, not on top of it
    CHECK(!t.c.promoteNode(t.root));      // roots can't go higher
}

TEST(canvas_demote_nests_into_previous_sibling) {
    Tree t;
    CHECK(t.c.demoteNode(t.sib));  // `a` comes first in reading order
    CHECK_EQ(t.c.getNode(t.sib)->parentId, t.a);
    CHECK_EQ(t.c.getNode(t.sib)->depth, 2);
    CHECK(!t.c.demoteNode(t.b));   // b has no previous sibling
}

TEST(canvas_remove_subtree_drops_descendants_and_connections) {
    Tree t;
    const std::string conn = t.c.createConnection(t.b, t.sib, "link");
    CHECK(!conn.empty());
    CHECK(t.c.removeSubtree(t.a));
    CHECK(!t.c.getNode(t.a));
    CHECK(!t.c.getNode(t.b));
    CHECK(!t.c.getConnection(conn));
    CHECK_EQ(t.c.getNode(t.root)->childrenUuids.size(), static_cast<size_t>(1));
}

TEST(canvas_frame_requires_common_parent_and_keeps_positions) {
    Tree t;
    CHECK(t.c.createFrame({t.a, t.b}).empty());  // different parents

    const Rect aBefore = t.c.computeWorldRect(t.a);
    const std::string frame = t.c.createFrame({t.a, t.sib}, "Group");
    CHECK(!frame.empty());
    CHECK(t.c.getNode(frame)->hasFrame);
    CHECK_EQ(t.c.getNode(t.a)->parentId, frame);
    CHECK_EQ(t.c.getNode(frame)->parentId, t.root);
    CHECK_EQ(t.c.getNode(t.b)->depth, 3);
    const Rect aAfter = t.c.computeWorldRect(t.a);
    CHECK_NEAR(aAfter.x, aBefore.x, 1e-9);
    CHECK_NEAR(aAfter.y, aBefore.y, 1e-9);
}

TEST(canvas_connection_rejects_self_and_missing_nodes) {
    Tree t;
    CHECK(t.c.createConnection(t.a, t.a).empty());
    CHECK(t.c.createConnection(t.a, "ghost").empty());
    CHECK(!t.c.createConnection(t.a, t.sib).empty());
}

TEST(canvas_rebuild_hierarchy_repairs_damaged_links) {
    Canvas c;
    auto make = [&](const std::string& id, const std::string& parent) {
        auto n = std::make_shared<Node>(id, "", Rect{0, 0, 10, 10});
        n->parentId = parent;
        c.insertNode(n);
    };
    make("x", "y");  // x <-> y cycle
    make("y", "x");
    make("z", "missing");
    make("w", "x");
    c.rebuildHierarchy();

    CHECK(c.getNode("z")->parentId.empty());
    const bool xRoot = c.getNode("x")->parentId.empty(), yRoot = c.getNode("y")->parentId.empty();
    CHECK(xRoot || yRoot);  // the cycle is cut somewhere
    CHECK(!c.isAncestorOf("x", "y") || !c.isAncestorOf("y", "x"));
    // Depths are consistent with the repaired links.
    for (const auto& [id, n] : c.allNodes()) {
        auto p = c.getNode(n->parentId);
        CHECK_EQ(n->depth, p ? p->depth + 1 : 0);
    }
}

TEST(canvas_resolve_overlaps_separates_siblings) {
    Canvas c;
    const std::string p = c.createNode("", {0, 0, 50, 30}, "p");
    const std::string a = c.createNode("", {5, 5, 20, 6}, "a", p);
    const std::string b = c.createNode("", {6, 6, 20, 6}, "b", p);
    c.resolveOverlaps(p);
    CHECK(!c.getNode(a)->rect.intersects(c.getNode(b)->rect));
    // The parent grew to keep both inside.
    CHECK(c.getNode(p)->rect.width >= c.getNode(b)->rect.right());
}

TEST(canvas_parent_grows_left_and_up_keeping_siblings_in_place) {
    Tree t;
    const Rect sibBefore = t.c.computeWorldRect(t.sib);
    const Rect rootBefore = t.c.computeWorldRect(t.root);

    // Pull `a` past root's left and top border.
    auto a = t.c.getNode(t.a);
    a->rect.x = -15;
    a->rect.y = -8;
    const Rect aWorld = t.c.computeWorldRect(t.a);
    t.c.expandParentToFit(t.a, 2.0);

    const Rect rootAfter = t.c.computeWorldRect(t.root);
    CHECK(rootAfter.x < rootBefore.x);  // left border moved out
    CHECK(rootAfter.y < rootBefore.y);  // top border moved out
    CHECK(rootAfter.x <= aWorld.x - 2.0 + 1e-9);
    CHECK_NEAR(rootAfter.right(), rootBefore.right(), 1e-9);  // right border unchanged

    // Nothing inside moved on screen.
    const Rect aAfter = t.c.computeWorldRect(t.a);
    CHECK_NEAR(aAfter.x, aWorld.x, 1e-9);
    CHECK_NEAR(aAfter.y, aWorld.y, 1e-9);
    CHECK_NEAR(t.c.computeWorldRect(t.sib).x, sibBefore.x, 1e-9);
    CHECK_NEAR(t.c.computeWorldRect(t.sib).y, sibBefore.y, 1e-9);
    CHECK(t.c.getNode(t.a)->rect.x >= 2.0 - 1e-9);  // a is inside root again
}

TEST(canvas_resolve_overlaps_grows_up_instead_of_shoving_child_down) {
    Canvas c;
    const std::string p = c.createNode("", {100, 100, 40, 30}, "p");
    const std::string a = c.createNode("", {5, 0.5, 10, 5}, "a", p);  // inside the header/text zone
    const Rect aWorld = c.computeWorldRect(a);
    c.resolveOverlaps(p);
    CHECK_NEAR(c.computeWorldRect(a).y, aWorld.y, 1e-9);  // the child stays where it was dropped
    CHECK(c.computeWorldRect(p).y < 100.0);              // the parent's top moved up
}

TEST(canvas_change_tracking_batches_until_flush) {
    Tree t;
    t.c.discardPendingChanges();
    int calls = 0;
    Canvas::ChangeSet seen;
    t.c.onChanged([&](const Canvas::ChangeSet& changes) {
        ++calls;
        seen = changes;
    });
    t.c.markChanged(t.a);
    t.c.processPendingChanges();  // still inside the throttle window
    CHECK_EQ(calls, 0);
    t.c.flushChanges();
    CHECK_EQ(calls, 1);
    CHECK(seen.nodes.count(t.a));
    t.c.flushChanges();  // nothing pending
    CHECK_EQ(calls, 1);
}

TEST(canvas_change_sets_are_as_small_as_possible) {
    Tree t;
    t.c.discardPendingChanges();
    Canvas::ChangeSet seen;
    t.c.onChanged([&](const Canvas::ChangeSet& changes) { seen = changes; });

    // A new box touches only itself and its parent: no config rewrite.
    const std::string n = t.c.createNode("", {1, 1, 5, 5}, "n", t.sib);
    t.c.flushChanges();
    CHECK(!seen.config && !seen.full);
    CHECK(seen.nodes.count(n) && seen.nodes.count(t.sib));

    // Deleting reports the removed files and, if links went, the config.
    t.c.createConnection(n, t.b);
    t.c.flushChanges();
    CHECK(seen.config);
    t.c.removeSubtree(t.sib);
    t.c.flushChanges();
    CHECK(seen.removed.count(t.sib) && seen.removed.count(n));
    CHECK(!seen.nodes.count(n));
    CHECK(seen.config);
}

TEST(node_auto_resize_measures_characters_not_bytes) {
    Node n("id", "Привет, мир", Rect{0, 0, 1, 1});  // 11 characters, 20 bytes
    n.autoResize();
    CHECK_NEAR(n.rect.width, 20.0, 1e-9);  // minimum width; byte counting would give 22
    Node wide("id", std::string(30, 'x'), Rect{0, 0, 1, 1});
    wide.autoResize();
    CHECK_NEAR(wide.rect.width, 32.0, 1e-9);
}
