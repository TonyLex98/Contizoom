#include "util/xml.h"
#include "util/utf8.h"

#include <cctype>
#include <cstdlib>

namespace ic::xml {

const std::string* Element::attr(const std::string& key) const {
    for (const auto& [k, v] : attributes) {
        if (k == key) return &v;
    }
    return nullptr;
}

std::string Element::attrOr(const std::string& key, const std::string& fallback) const {
    const std::string* v = attr(key);
    return v ? *v : fallback;
}

const Element* Element::child(const std::string& childName) const {
    for (const auto& c : children) {
        if (c->name == childName) return c.get();
    }
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    std::unique_ptr<Element> document() {
        // Skip a UTF-8 byte order mark.
        if (s_.compare(0, 3, "\xEF\xBB\xBF") == 0) pos_ = 3;
        skipMisc();
        if (pos_ >= s_.size() || s_[pos_] != '<') fail("expected root element");
        auto root = element(0);
        skipMisc();
        if (pos_ != s_.size()) fail("content after the root element");
        return root;
    }

private:
    static constexpr int kMaxDepth = 512;
    const std::string& s_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& what) const { throw ParseError(what, pos_); }

    bool startsWith(const char* lit) const {
        size_t n = 0;
        while (lit[n]) ++n;
        return s_.compare(pos_, n, lit) == 0;
    }

    void skipSpace() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }

    void skipPast(const char* terminator) {
        const size_t end = s_.find(terminator, pos_);
        if (end == std::string::npos) fail(std::string("unterminated construct, expected ") + terminator);
        pos_ = end + std::string(terminator).size();
    }

    // Whitespace, comments, processing instructions, DOCTYPE.
    void skipMisc() {
        while (true) {
            skipSpace();
            if (startsWith("<?")) skipPast("?>");
            else if (startsWith("<!--")) skipPast("-->");
            else if (startsWith("<!DOCTYPE") || startsWith("<!doctype")) skipPast(">");
            else return;
        }
    }

    std::string name() {
        const size_t start = pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (std::isspace(static_cast<unsigned char>(c)) || c == '=' || c == '>' || c == '/' || c == '<') break;
            ++pos_;
        }
        if (pos_ == start) fail("expected a name");
        return s_.substr(start, pos_ - start);
    }

    std::string decodeEntities(const std::string& raw) const {
        std::string out;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '&') {
                out += raw[i];
                continue;
            }
            const size_t semi = raw.find(';', i);
            if (semi == std::string::npos) {
                out += raw[i];  // lenient: a bare ampersand
                continue;
            }
            const std::string ent = raw.substr(i + 1, semi - i - 1);
            if (ent == "amp") out += '&';
            else if (ent == "lt") out += '<';
            else if (ent == "gt") out += '>';
            else if (ent == "quot") out += '"';
            else if (ent == "apos") out += '\'';
            else if (!ent.empty() && ent[0] == '#') {
                const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
                const unsigned long cp = std::strtoul(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
                if (cp > 0 && cp <= 0x10FFFF) utf8::append(out, static_cast<char32_t>(cp));
            } else {
                out += raw.substr(i, semi - i + 1);  // unknown entity: keep as is
            }
            i = semi;
        }
        return out;
    }

    std::unique_ptr<Element> element(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        ++pos_;  // '<'
        auto el = std::make_unique<Element>();
        el->name = name();

        // Attributes.
        while (true) {
            skipSpace();
            if (pos_ >= s_.size()) fail("unterminated start tag");
            if (s_[pos_] == '/') {
                if (pos_ + 1 >= s_.size() || s_[pos_ + 1] != '>') fail("expected '>'");
                pos_ += 2;
                return el;  // empty element
            }
            if (s_[pos_] == '>') {
                ++pos_;
                break;
            }
            std::string key = name();
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != '=') fail("expected '=' after attribute name");
            ++pos_;
            skipSpace();
            if (pos_ >= s_.size() || (s_[pos_] != '"' && s_[pos_] != '\'')) fail("expected quoted attribute value");
            const char quote = s_[pos_++];
            const size_t end = s_.find(quote, pos_);
            if (end == std::string::npos) fail("unterminated attribute value");
            el->attributes.emplace_back(std::move(key), decodeEntities(s_.substr(pos_, end - pos_)));
            pos_ = end + 1;
        }

        // Content.
        while (true) {
            if (pos_ >= s_.size()) fail("unterminated element <" + el->name + ">");
            if (startsWith("</")) {
                pos_ += 2;
                const std::string closing = name();
                if (closing != el->name) fail("mismatched </" + closing + ">, expected </" + el->name + ">");
                skipSpace();
                if (pos_ >= s_.size() || s_[pos_] != '>') fail("expected '>'");
                ++pos_;
                return el;
            }
            if (startsWith("<!--")) {
                skipPast("-->");
            } else if (startsWith("<![CDATA[")) {
                pos_ += 9;
                const size_t end = s_.find("]]>", pos_);
                if (end == std::string::npos) fail("unterminated CDATA");
                el->text += s_.substr(pos_, end - pos_);
                pos_ = end + 3;
            } else if (startsWith("<?")) {
                skipPast("?>");
            } else if (s_[pos_] == '<') {
                el->children.push_back(element(depth + 1));
            } else {
                const size_t end = s_.find('<', pos_);
                el->text += decodeEntities(s_.substr(pos_, end == std::string::npos ? std::string::npos : end - pos_));
                pos_ = end == std::string::npos ? s_.size() : end;
            }
        }
    }
};

}  // namespace

std::unique_ptr<Element> parse(const std::string& text) {
    return Parser(text).document();
}

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            case '\n': out += "&#10;"; break;
            case '\r': break;
            case '\t': out += "&#9;"; break;
            default: out += c;
        }
    }
    return out;
}

}  // namespace ic::xml
