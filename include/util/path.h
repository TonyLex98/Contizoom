#ifndef IC_PATH_H
#define IC_PATH_H

// The program keeps all strings in UTF-8, including file paths. On Windows
// std::filesystem::path(std::string) would interpret bytes in the ANSI code
// page and mangle non-ASCII names, so conversions go through these helpers.

#include <filesystem>
#include <string>

namespace ic::path {

inline std::filesystem::path fromUtf8(const std::string& s) {
    return std::filesystem::u8path(s);
}

inline std::string toUtf8(const std::filesystem::path& p) {
    return p.u8string();
}

}  // namespace ic::path

#endif  // IC_PATH_H
