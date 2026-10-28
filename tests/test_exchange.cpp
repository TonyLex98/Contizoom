#include "test.h"
#include "model/arrange.h"
#include "model/canvas.h"
#include "storage/exchange.h"
#include "util/xml.h"

using namespace ic;

namespace {

std::shared_ptr<Node> byName(const Canvas& c, const std::string& name) {
    for (const auto& [_, n] : c.allNodes()) {
        if (n->name == name) return n;
    }
    return nullptr;
}

// Plan
//   Goals ("Line one\nLine two")
//     Ship it
//     Test it
//   Done
// Ship it -> Done "leads to"
Canvas sampleCanvas() {
    Canvas c;
    c.canvasName = "Plan";
    const std::string goals = c.createNode("Line one\nLine two", {0, 0, 40, 20}, "Goals");
    const std::string ship = c.createNode("", {2, 5, 10, 5}, "Ship it", goals);
    c.createNode("", {20, 5, 10, 5}, "Test it", goals);
    const std::string done = c.createNode("", {60, 0, 10, 5}, "Done");
    c.createConnection(ship, done, "leads to");
    return c;
}

// The structure that survives every outline format.
void checkSampleStructure(const Canvas& c, bool withText, bool withLinks) {
    CHECK_EQ(c.allNodes().size(), size_t(4));
    auto goals = byName(c, "Goals");
    auto ship = byName(c, "Ship it");
    auto test = byName(c, "Test it");
    auto done = byName(c, "Done");
    CHECK(goals && ship && test && done);
    if (!goals || !ship || !test || !done) return;
    CHECK(goals->parentId.empty());
    CHECK(done->parentId.empty());
    CHECK_EQ(ship->parentId, goals->uuid);
    CHECK_EQ(test->parentId, goals->uuid);
    if (withText) CHECK_EQ(goals->text, std::string("Line one\nLine two"));
    if (withLinks) {
        CHECK_EQ(c.allConnections().size(), size_t(1));
        for (const auto& [_, conn] : c.allConnections()) {
            CHECK_EQ(conn->fromUuid, ship->uuid);
            CHECK_EQ(conn->toUuid, done->uuid);
            CHECK_EQ(conn->name, std::string("leads to"));
        }
    }
}

Canvas importAs(const std::string& path, const std::string& content) {
    Canvas c;
    std::string error;
    CHECK(exchange::importText(path, content, c, error));
    CHECK_EQ(error, std::string(""));
    return c;
}

}  // namespace

// ---- XML -----------------------------------------------------------

TEST(xml_parses_elements_attributes_and_entities) {
    auto root = xml::parse(
        "\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<!-- c --><!DOCTYPE x>\n"
        "<a k='1 &amp; 2' q=\"&#x41;&#66;\"><b>t &lt;x&gt;</b><b/><![CDATA[<raw>]]></a>");
    CHECK_EQ(root->name, std::string("a"));
    CHECK_EQ(root->attrOr("k"), std::string("1 & 2"));
    CHECK_EQ(root->attrOr("q"), std::string("AB"));
    CHECK_EQ(root->attrOr("missing", "-"), std::string("-"));
    CHECK_EQ(root->children.size(), size_t(2));
    CHECK_EQ(root->child("b")->text, std::string("t <x>"));
    CHECK_EQ(root->text, std::string("<raw>"));
}

TEST(xml_rejects_broken_documents) {
    CHECK_THROWS(xml::parse("<a><b></a>"), xml::ParseError);
    CHECK_THROWS(xml::parse("<a"), xml::ParseError);
    CHECK_THROWS(xml::parse(""), xml::ParseError);
}

TEST(xml_escape_round_trips) {
    const std::string s = "a<b>&\"c'\nd";
    auto root = xml::parse("<x v=\"" + xml::escape(s) + "\"/>");
    CHECK_EQ(root->attrOr("v"), s);
}

// ---- Format detection ------------------------------------------------

TEST(exchange_detects_format_by_extension_then_content) {
    using exchange::Format;
    CHECK(exchange::detect("a.md", "") == Format::Markdown);
    CHECK(exchange::detect("A.OPML", "") == Format::Opml);
    CHECK(exchange::detect("a.json", "") == Format::Bundle);
    CHECK(exchange::detect("a.txt", "") == Format::PlainText);
    CHECK(exchange::detect("a.xml", "<?xml?><opml version=\"2.0\"/>") == Format::Opml);
    CHECK(exchange::detect("noext", "# Title") == Format::Markdown);
    CHECK(exchange::detect("noext", "  {\"format\":1}") == Format::Bundle);
    CHECK(exchange::detect("noext", "just lines") == Format::PlainText);
    CHECK(!exchange::detect("noext", "  \n "));
}

// ---- Round trips -----------------------------------------------------

TEST(exchange_markdown_round_trips_structure_text_and_links) {
    const std::string md = exchange::toMarkdown(sampleCanvas());
    const Canvas back = importAs("plan.md", md);
    CHECK_EQ(back.canvasName, std::string("Plan"));
    checkSampleStructure(back, true, true);
}

TEST(exchange_markdown_imports_lists_and_deep_levels) {
    const Canvas c = importAs("x.md",
                              "# Notes\n\n"
                              "## Inbox\n\n"
                              "- Milk\n"
                              "  - Oat\n"
                              "* Bread\n"
                              "1. Eggs\n");
    CHECK_EQ(c.canvasName, std::string("Notes"));
    auto inbox = byName(c, "Inbox");
    auto oat = byName(c, "Oat");
    auto milk = byName(c, "Milk");
    CHECK(inbox && oat && milk && byName(c, "Bread") && byName(c, "Eggs"));
    if (inbox && milk && oat) {
        CHECK_EQ(milk->parentId, inbox->uuid);
        CHECK_EQ(oat->parentId, milk->uuid);
        CHECK_EQ(byName(c, "Eggs")->parentId, inbox->uuid);
    }
}

TEST(exchange_opml_round_trips_structure_and_notes) {
    const std::string opml = exchange::toOpml(sampleCanvas());
    CHECK(opml.find("<opml version=\"2.0\">") != std::string::npos);
    const Canvas back = importAs("plan.opml", opml);
    CHECK_EQ(back.canvasName, std::string("Plan"));
    checkSampleStructure(back, true, false);
}

TEST(exchange_opml_rejects_other_xml) {
    Canvas c;
    std::string error;
    CHECK(!exchange::importText("x.opml", "<html><body/></html>", c, error));
    CHECK(!error.empty());
    CHECK(!exchange::importText("x.opml", "<opml><body>", c, error));
}

TEST(exchange_plain_text_round_trips_hierarchy) {
    const std::string txt = exchange::toPlainText(sampleCanvas());
    const Canvas back = importAs("plan.txt", txt);
    checkSampleStructure(back, false, false);
}

TEST(exchange_plain_text_handles_tabs_and_uneven_indents) {
    const Canvas c = importAs("x.txt", "A\n\tB\n\t\tC\n   D\nE\n\n");
    auto a = byName(c, "A"), b = byName(c, "B"), cc = byName(c, "C"), d = byName(c, "D"), e = byName(c, "E");
    CHECK(a && b && cc && d && e);
    if (!(a && b && cc && d && e)) return;
    CHECK_EQ(b->parentId, a->uuid);
    CHECK_EQ(cc->parentId, b->uuid);
    CHECK(e->parentId.empty());
    CHECK(!d->parentId.empty());  // indented under A's subtree
}

TEST(exchange_bundle_restores_the_canvas_exactly) {
    Canvas c = sampleCanvas();
    c.viewport.centerX = 12.5;
    c.viewport.scale = 2.0;
    const Canvas back = importAs("plan.json", exchange::toBundle(c));
    checkSampleStructure(back, true, true);
    CHECK_EQ(back.canvasName, std::string("Plan"));
    CHECK_NEAR(back.viewport.centerX, 12.5, 1e-9);
    CHECK_NEAR(back.viewport.scale, 2.0, 1e-9);
    CHECK_NEAR(byName(back, "Done")->rect.x, 60.0, 1e-9);
    CHECK_NEAR(byName(back, "Ship it")->rect.y, 5.0, 1e-9);
}

TEST(exchange_bundle_rejects_foreign_json) {
    Canvas c;
    std::string error;
    CHECK(!exchange::importText("x.json", "{\"hello\":1}", c, error));
    CHECK(!error.empty());
    CHECK(!exchange::importText("x.json", "{broken", c, error));
}

TEST(exchange_links_to_unknown_titles_are_dropped) {
    exchange::Outline o;
    o.items.push_back({"A", "", {}});
    o.links.push_back({"A", "Nowhere", "x", true});
    Canvas c;
    exchange::buildCanvas(o, c);
    CHECK_EQ(c.allNodes().size(), size_t(1));
    CHECK(c.allConnections().empty());
}

// ---- Layout ----------------------------------------------------------

TEST(arrange_tree_keeps_children_inside_parents_without_overlaps) {
    exchange::Outline o;
    o.title = "Big";
    for (int i = 0; i < 4; ++i) {
        exchange::OutlineItem root{"Root " + std::to_string(i), "Some text\nover two lines", {}};
        for (int j = 0; j < 5; ++j) {
            exchange::OutlineItem child{"Child " + std::to_string(i) + "." + std::to_string(j), "", {}};
            if (j % 2 == 0) child.children.push_back({"Leaf " + std::to_string(j), "deep", {}});
            root.children.push_back(child);
        }
        o.items.push_back(root);
    }
    Canvas c;
    exchange::buildCanvas(o, c);
    CHECK_EQ(c.allNodes().size(), size_t(4 + 20 + 12));

    for (const auto& [_, n] : c.allNodes()) {
        CHECK(n->rect.width > 0 && n->rect.height > 0);
        if (!n->parentId.empty()) {
            auto p = c.getNode(n->parentId);
            CHECK(n->rect.x >= 0 && n->rect.y >= 0);
            CHECK(n->rect.right() <= p->rect.width + 1e-9);
            CHECK(n->rect.bottom() <= p->rect.height + 1e-9);
        }
        // Siblings never overlap.
        for (const auto& s : c.getChildren(n->parentId)) {
            if (s->uuid == n->uuid) continue;
            const Rect& a = n->rect;
            const Rect& b = s->rect;
            const bool apart = a.right() <= b.left() || b.right() <= a.left() || a.bottom() <= b.top() ||
                               b.bottom() <= a.top();
            CHECK(apart);
        }
    }
}

TEST(arrange_tree_keeps_reading_order) {
    exchange::Outline o;
    for (const char* t : {"First", "Second", "Third"}) o.items.push_back({t, "", {}});
    Canvas c;
    exchange::buildCanvas(o, c);
    auto first = byName(c, "First"), second = byName(c, "Second"), third = byName(c, "Third");
    auto before = [](const Node& a, const Node& b) {
        return a.rect.y < b.rect.y || (a.rect.y == b.rect.y && a.rect.x < b.rect.x);
    };
    CHECK(before(*first, *second));
    CHECK(before(*second, *third));
    // The sidebar outline follows the same order.
    const auto roots = c.getChildren("");
    CHECK_EQ(roots.front()->name, std::string("First"));
    CHECK_EQ(roots.back()->name, std::string("Third"));
}
