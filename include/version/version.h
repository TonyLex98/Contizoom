#ifndef INFINITE_CANVAS_VERSION_H
#define INFINITE_CANVAS_VERSION_H

#include <utility>
#include <string>

// ============================================================================
// Software version
// ============================================================================
#define IC_VERSION_MAJOR 0
#define IC_VERSION_MINOR 1
#define IC_VERSION_PATCH 0

#define IC_VERSION_STRING "0.1.0"

// ============================================================================
// Storage format version (semantic versioning for data files)
// ============================================================================
#define IC_STORAGE_FORMAT_MAJOR 1
#define IC_STORAGE_FORMAT_MINOR 0

#define IC_STORAGE_FORMAT_STRING "1.0"

// Migration tracking: list of all supported format versions
// If loaded file version < IC_STORAGE_FORMAT, migration is attempted.
#define IC_STORAGE_MIN_SUPPORTED_MAJOR 1
#define IC_STORAGE_MIN_SUPPORTED_MINOR 0

// ============================================================================
// Version helpers
// ============================================================================
inline bool operator<(std::pair<int, int> a, std::pair<int, int> b) {
    return a.first < b.first || (a.first == b.first && a.second < b.second);
}

#endif // INFINITE_CANVAS_VERSION_H
