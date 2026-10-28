#ifndef IC_VERSION_H
#define IC_VERSION_H

// Application version.
#define IC_VERSION_MAJOR 0
#define IC_VERSION_MINOR 2
#define IC_VERSION_PATCH 0
#define IC_VERSION_STRING "0.2.0"

// On-disk project format. A reader accepts any file with the same major
// version; minor bumps only add optional fields.
//   1.0 - initial format
//   1.1 - explicit "format" field; derived zoom levels no longer stored
#define IC_STORAGE_FORMAT_MAJOR 1
#define IC_STORAGE_FORMAT_MINOR 1
#define IC_STORAGE_FORMAT_STRING "1.1"

#endif  // IC_VERSION_H
