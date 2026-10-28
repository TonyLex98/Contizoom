#ifndef IC_XML_H
#define IC_XML_H

// Minimal XML reader/writer, enough for OPML: elements, attributes, text,
// the five predefined entities and numeric character references. Comments,
// processing instructions and DOCTYPE are skipped; namespaces are kept as
// part of the name. Not a validating parser.

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ic::xml {

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& what, size_t offset)
        : std::runtime_error(what + " at offset " + std::to_string(offset)) {}
};

struct Element {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<std::unique_ptr<Element>> children;
    std::string text;  // concatenated character data directly inside

    const std::string* attr(const std::string& key) const;
    std::string attrOr(const std::string& key, const std::string& fallback = "") const;
    const Element* child(const std::string& childName) const;
};

// Parses a document and returns its root element; throws ParseError.
std::unique_ptr<Element> parse(const std::string& text);

// Escapes text for use inside an attribute value or element content.
std::string escape(const std::string& s);

}  // namespace ic::xml

#endif  // IC_XML_H
