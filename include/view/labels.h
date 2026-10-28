#ifndef IC_LABELS_H
#define IC_LABELS_H

// Translated names for model enums, shared by the renderer and the menus.
// Lives in the view layer so neither the model nor i18n depends on the other.

#include "i18n/i18n.h"
#include "model/connection.h"

namespace ic {

inline Msg styleLabel(ConnType t) {
    switch (t) {
        case ConnType::Arrow: return Msg::StyleArrow;
        case ConnType::DashedArrow: return Msg::StyleDashedArrow;
        case ConnType::DottedArrow: return Msg::StyleDottedArrow;
        case ConnType::Line: return Msg::StyleLine;
        case ConnType::DashedLine: return Msg::StyleDashedLine;
        case ConnType::DottedLine: return Msg::StyleDottedLine;
    }
    return Msg::StyleArrow;
}

// A few cells showing what the style looks like, for menus.
inline const char* styleSample(ConnType t) {
    switch (t) {
        case ConnType::Arrow: return "──►";
        case ConnType::DashedArrow: return "--►";
        case ConnType::DottedArrow: return "··►";
        case ConnType::Line: return "───";
        case ConnType::DashedLine: return "---";
        case ConnType::DottedLine: return "···";
    }
    return "";
}

}  // namespace ic

#endif  // IC_LABELS_H
