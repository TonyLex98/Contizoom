#ifndef IC_STRUTIL_H
#define IC_STRUTIL_H

#include <string>
#include <algorithm>
#include <cctype>

namespace ic {

inline std::string& ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](unsigned char ch) { return !std::isspace(ch); }));
    return s;
}

inline std::string& rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
            [](unsigned char ch) { return !std::isspace(ch); }).base(),
            s.end());
    return s;
}

inline std::string trim(std::string s) {
    return ltrim(rtrim(s));
}

} // namespace ic

#endif // IC_STRUTIL_H
