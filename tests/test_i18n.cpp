#include "test.h"
#include "app/demo.h"
#include "i18n/i18n.h"
#include "model/canvas.h"
#include "util/utf8.h"
#include "view/cell_buffer.h"
#include "view/console_renderer.h"

#include <set>

using namespace ic;
using i18n::Language;

namespace {

constexpr int kLanguages = static_cast<int>(Language::Count);
constexpr int kMessages = static_cast<int>(Msg::Count);

// Switches the UI language for one test and restores English after.
struct LanguageScope {
    explicit LanguageScope(Language l) { i18n::setLanguage(l); }
    ~LanguageScope() { i18n::setLanguage(Language::English); }
};

std::set<std::string> placeholders(const std::string& s) {
    std::set<std::string> out;
    for (size_t p = s.find('{'); p != std::string::npos; p = s.find('{', p + 1)) {
        const size_t e = s.find('}', p);
        if (e != std::string::npos) out.insert(s.substr(p, e - p + 1));
    }
    return out;
}

}  // namespace

TEST(i18n_every_message_is_translated_in_every_language) {
    for (int m = 0; m < kMessages; ++m) {
        for (int l = 0; l < kLanguages; ++l) {
            const std::string text = tr(static_cast<Msg>(m), static_cast<Language>(l));
            if (text.empty()) {
                CHECK(!text.empty());
                std::cerr << "    message #" << m << " is empty in " << i18n::code(static_cast<Language>(l)) << "\n";
            }
        }
    }
}

TEST(i18n_translations_keep_the_same_placeholders) {
    for (int m = 0; m < kMessages; ++m) {
        const auto expected = placeholders(tr(static_cast<Msg>(m), Language::English));
        for (int l = 1; l < kLanguages; ++l) {
            const auto got = placeholders(tr(static_cast<Msg>(m), static_cast<Language>(l)));
            if (got != expected) {
                CHECK(got == expected);
                std::cerr << "    message #" << m << " placeholders differ in " << i18n::code(static_cast<Language>(l))
                          << "\n";
            }
        }
    }
}

TEST(i18n_formats_arguments_and_switches_language) {
    CHECK_EQ(tr(Msg::Boxes, {"3"}), std::string("3 boxes"));
    {
        LanguageScope ru(Language::Russian);
        CHECK_EQ(tr(Msg::Boxes, {"3"}), std::string("блоков: 3"));
        CHECK_EQ(tr(Msg::StatusLocked, {"Plan", "42"}).find("42") != std::string::npos, true);
    }
    CHECK_EQ(std::string(tr(Msg::MenuQuit)), std::string("Quit"));
}

TEST(i18n_parses_locale_names) {
    CHECK(i18n::parse("ru") == Language::Russian);
    CHECK(i18n::parse("ru_RU.UTF-8") == Language::Russian);
    CHECK(i18n::parse("zh-CN") == Language::Chinese);
    CHECK(i18n::parse("FR") == Language::French);
    CHECK(i18n::parse("es_MX") == Language::Spanish);
    CHECK(!i18n::parse("de").has_value());
    CHECK(!i18n::parse("").has_value());
    for (int l = 0; l < kLanguages; ++l) {
        CHECK(i18n::parse(i18n::code(static_cast<Language>(l))) == static_cast<Language>(l));
    }
}

TEST(i18n_wide_characters_take_two_cells) {
    CHECK_EQ(utf8::width("abc"), 3);
    CHECK_EQ(utf8::width("中文"), 4);
    CHECK_EQ(utf8::width("Привет"), 6);
    CHECK_EQ(utf8::fitWidth("中文字", 5), std::string("中文"));  // a third would need 6

    CellBuffer b;
    b.resize(10, 1);
    CHECK_EQ(b.text(0, 0, "a中b", color::Default), 4);
    CHECK(b.at(1, 0).part == CellBuffer::Part::Lead);
    CHECK(b.at(2, 0).part == CellBuffer::Part::Trail);
    CHECK_EQ(b.toText(), std::string("a中b\n"));

    // Overwriting half of a wide character clears the other half.
    b.put(2, 0, L'x', color::Default);
    CHECK(b.at(1, 0).part == CellBuffer::Part::Single);
    CHECK_EQ(b.toText(), std::string("a xb\n"));

    // A wide character that doesn't fit isn't half-drawn.
    CellBuffer narrow;
    narrow.resize(3, 1);
    CHECK_EQ(narrow.text(0, 0, "ab中", color::Default, 3), 2);
}

TEST(i18n_interface_renders_in_each_language) {
    for (int l = 0; l < kLanguages; ++l) {
        LanguageScope scope(static_cast<Language>(l));
        Canvas c;
        buildDemoCanvas(c);
        CHECK_EQ(c.canvasName, std::string(tr(Msg::DemoName)));

        const std::string text = renderSnapshot(c, 120, 40);
        CHECK(text.find(tr(Msg::SidebarTitle)) != std::string::npos);
        CHECK(text.find(tr(Msg::HudZoom)) != std::string::npos);
        CHECK(text.find(tr(Msg::DemoBasics)) != std::string::npos);

        // No row may overflow the screen, even with wide characters.
        size_t start = 0;
        while (start < text.size()) {
            const size_t end = text.find('\n', start);
            CHECK(utf8::width(text.substr(start, end - start)) <= 120);
            start = end + 1;
        }
    }
}
