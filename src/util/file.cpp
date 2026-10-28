#include "util/file.h"

#include <cstdio>

namespace ic::file {

namespace {

std::FILE* open(const std::filesystem::path& p, bool forWrite) {
#ifdef _WIN32
    return _wfopen(p.c_str(), forWrite ? L"wb" : L"rb");
#else
    return std::fopen(p.c_str(), forWrite ? "wb" : "rb");
#endif
}

}  // namespace

bool read(const std::filesystem::path& p, std::string& out) {
    std::FILE* f = open(p, false);
    if (!f) return false;
    out.clear();
    char buf[16384];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

bool write(const std::filesystem::path& p, const std::string& content) {
    std::FILE* f = open(p, true);
    if (!f) return false;
    const bool wrote = std::fwrite(content.data(), 1, content.size(), f) == content.size();
    const bool flushed = std::fflush(f) == 0;
    const bool closed = std::fclose(f) == 0;
    return wrote && flushed && closed;
}

}  // namespace ic::file
