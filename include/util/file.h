#ifndef IC_FILE_H
#define IC_FILE_H

// Whole-file I/O on std::FILE. Deliberately not iostreams: with the
// runtime linked statically, <fstream>/<sstream> pull in the locale
// machinery and more than double the size of the executable. Paths go
// through std::filesystem::path so UTF-8 names work on Windows (_wfopen).

#include <filesystem>
#include <string>

namespace ic::file {

bool read(const std::filesystem::path& p, std::string& out);
bool write(const std::filesystem::path& p, const std::string& content);  // flushes; false on any error

}  // namespace ic::file

#endif  // IC_FILE_H
