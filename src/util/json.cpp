#include "util/json.h"
#include "util/utf8.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace ic::json {

// ====================================================================
// Value
// ====================================================================

const Value* Value::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& [k, v] : object_) {
        if (k == key) return &v;
    }
    return nullptr;
}

Value& Value::set(const std::string& key, Value value) {
    if (type_ != Type::Object) {
        *this = Value(Object{});
    }
    for (auto& [k, v] : object_) {
        if (k == key) {
            v = std::move(value);
            return v;
        }
    }
    object_.emplace_back(key, std::move(value));
    return object_.back().second;
}

std::string Value::getString(const std::string& key, const std::string& fallback) const {
    const Value* v = find(key);
    return v && v->isString() ? v->asString() : fallback;
}

double Value::getNumber(const std::string& key, double fallback) const {
    const Value* v = find(key);
    return v && v->isNumber() && std::isfinite(v->asNumber()) ? v->asNumber() : fallback;
}

int Value::getInt(const std::string& key, int fallback) const {
    const double d = getNumber(key, static_cast<double>(fallback));
    if (d < std::numeric_limits<int>::min() || d > std::numeric_limits<int>::max()) return fallback;
    return static_cast<int>(d);
}

uint64_t Value::getUInt64(const std::string& key, uint64_t fallback) const {
    const double d = getNumber(key, -1.0);
    if (d < 0.0 || d > 1.8e19) return fallback;
    return static_cast<uint64_t>(d);
}

bool Value::getBool(const std::string& key, bool fallback) const {
    const Value* v = find(key);
    return v && v->isBool() ? v->asBool() : fallback;
}

// ====================================================================
// Parser
// ====================================================================

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    Value parseDocument() {
        skipWhitespace();
        Value v = parseValue(0);
        skipWhitespace();
        if (pos_ != s_.size()) fail("unexpected trailing characters");
        return v;
    }

private:
    static constexpr int kMaxDepth = 256;
    const std::string& s_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& what) const { throw ParseError(what, pos_); }

    void skipWhitespace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool consume(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) fail(std::string("expected '") + c + "'");
    }

    bool consumeLiteral(const char* lit) {
        size_t n = 0;
        while (lit[n]) ++n;
        if (s_.compare(pos_, n, lit) != 0) return false;
        pos_ += n;
        return true;
    }

    Value parseValue(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        if (pos_ >= s_.size()) fail("unexpected end of input");
        const char c = s_[pos_];
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') return Value(parseString());
        if (c == '-' || (c >= '0' && c <= '9')) return Value(parseNumber());
        if (consumeLiteral("true")) return Value(true);
        if (consumeLiteral("false")) return Value(false);
        if (consumeLiteral("null")) return Value(nullptr);
        fail("unexpected character");
    }

    Value parseObject(int depth) {
        expect('{');
        Value obj = Value::object();
        skipWhitespace();
        if (consume('}')) return obj;
        while (true) {
            skipWhitespace();
            if (pos_ >= s_.size() || s_[pos_] != '"') fail("expected object key");
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            skipWhitespace();
            obj.set(key, parseValue(depth + 1));
            skipWhitespace();
            if (consume('}')) return obj;
            expect(',');
        }
    }

    Value parseArray(int depth) {
        expect('[');
        Value arr = Value::array();
        skipWhitespace();
        if (consume(']')) return arr;
        while (true) {
            skipWhitespace();
            arr.push(parseValue(depth + 1));
            skipWhitespace();
            if (consume(']')) return arr;
            expect(',');
        }
    }

    unsigned parseHex4() {
        if (pos_ + 4 > s_.size()) fail("truncated \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid hex digit in \\u escape");
        }
        return v;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= s_.size()) fail("unterminated string");
            const char c = s_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) fail("unterminated escape");
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    char32_t cp = parseHex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // High surrogate: must be followed by \uDC00-\uDFFF.
                        if (!(consume('\\') && consume('u'))) fail("unpaired surrogate");
                        const char32_t lo = parseHex4();
                        if (lo < 0xDC00 || lo > 0xDFFF) fail("invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        fail("unpaired surrogate");
                    }
                    utf8::append(out, cp);
                    break;
                }
                default: fail("invalid escape");
            }
        }
    }

    double parseNumber() {
        const size_t start = pos_;
        consume('-');
        if (consume('0')) {
            // A leading zero may not be followed by more digits.
        } else if (pos_ < s_.size() && s_[pos_] >= '1' && s_[pos_] <= '9') {
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        } else {
            fail("invalid number");
        }
        if (consume('.')) {
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) fail("invalid fraction");
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (!consume('+')) consume('-');
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) fail("invalid exponent");
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        // strtod is locale-sensitive in theory; the program never calls
        // setlocale, so the "C" locale (with '.' as separator) is in effect.
        const std::string token = s_.substr(start, pos_ - start);
        return std::strtod(token.c_str(), nullptr);
    }
};

// ====================================================================
// Serializer
// ====================================================================

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void writeNumber(std::string& out, double d) {
    if (!std::isfinite(d)) {
        out += "null";  // JSON has no NaN/Infinity
        return;
    }
    char buf[32];
    if (std::fabs(d) < 1e15 && d == std::floor(d)) {
        std::snprintf(buf, sizeof(buf), "%.0f", d);
    } else {
        // Shortest representation that round-trips exactly.
        std::snprintf(buf, sizeof(buf), "%.15g", d);
        if (std::strtod(buf, nullptr) != d) std::snprintf(buf, sizeof(buf), "%.17g", d);
    }
    out += buf;
}

void writeValue(std::string& out, const Value& v, int indent, int level) {
    const auto newline = [&](int lvl) {
        if (indent <= 0) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * lvl), ' ');
    };

    switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Value::Type::Number: writeNumber(out, v.asNumber()); break;
        case Value::Type::String: writeString(out, v.asString()); break;
        case Value::Type::Array: {
            const auto& arr = v.asArray();
            if (arr.empty()) {
                out += "[]";
                break;
            }
            out += '[';
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i > 0) out += ',';
                newline(level + 1);
                writeValue(out, arr[i], indent, level + 1);
            }
            newline(level);
            out += ']';
            break;
        }
        case Value::Type::Object: {
            const auto& obj = v.asObject();
            if (obj.empty()) {
                out += "{}";
                break;
            }
            out += '{';
            for (size_t i = 0; i < obj.size(); ++i) {
                if (i > 0) out += ',';
                newline(level + 1);
                writeString(out, obj[i].first);
                out += indent > 0 ? ": " : ":";
                writeValue(out, obj[i].second, indent, level + 1);
            }
            newline(level);
            out += '}';
            break;
        }
    }
}

}  // namespace

Value parse(const std::string& text) {
    return Parser(text).parseDocument();
}

std::string serialize(const Value& value, int indent) {
    std::string out;
    writeValue(out, value, indent, 0);
    if (indent > 0) out += '\n';
    return out;
}

}  // namespace ic::json
