#ifndef IC_JSON_H
#define IC_JSON_H

// Small self-contained JSON value, parser and serializer (RFC 8259).
// Objects keep insertion order so saved files stay stable and diff-friendly.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ic::json {

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& what, size_t offset)
        : std::runtime_error(what + " at offset " + std::to_string(offset)), offset_(offset) {}
    size_t offset() const { return offset_; }

private:
    size_t offset_;
};

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(double d) : type_(Type::Number), number_(d) {}
    Value(int i) : type_(Type::Number), number_(i) {}
    Value(int64_t i) : type_(Type::Number), number_(static_cast<double>(i)) {}
    Value(uint64_t u) : type_(Type::Number), number_(static_cast<double>(u)) {}
    Value(const char* s) : type_(Type::String), string_(s) {}
    Value(std::string s) : type_(Type::String), string_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), array_(std::move(a)) {}
    Value(Object o) : type_(Type::Object), object_(std::move(o)) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const { return bool_; }
    double asNumber() const { return number_; }
    const std::string& asString() const { return string_; }
    const Array& asArray() const { return array_; }
    const Object& asObject() const { return object_; }

    // Object access. find() returns nullptr when this isn't an object or the
    // key is absent; set() replaces an existing key or appends a new one.
    const Value* find(const std::string& key) const;
    Value& set(const std::string& key, Value value);

    // Array access.
    void push(Value value) { array_.push_back(std::move(value)); }

    // Typed lookups with a fallback for missing or mistyped fields, so a
    // loader can read older/partial files without a cascade of checks.
    std::string getString(const std::string& key, const std::string& fallback = "") const;
    double getNumber(const std::string& key, double fallback = 0.0) const;
    int getInt(const std::string& key, int fallback = 0) const;
    uint64_t getUInt64(const std::string& key, uint64_t fallback = 0) const;
    bool getBool(const std::string& key, bool fallback = false) const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

// Parses a complete JSON document; throws ParseError on malformed input.
Value parse(const std::string& text);

// Serializes with `indent` spaces per level (0 = compact, single line).
std::string serialize(const Value& value, int indent = 2);

}  // namespace ic::json

#endif  // IC_JSON_H
