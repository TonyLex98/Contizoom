#ifndef IC_HISTORY_H
#define IC_HISTORY_H

#include "i18n/i18n.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ic {

class Canvas;

// Undo/redo by document snapshots. Documents are small (a few KB of JSON),
// so storing the whole content per step is simpler and far more robust
// than inverse operations: every action, however complex, undoes exactly.
//
// Usage: take a snapshot before an action, then record() it afterwards;
// record() ignores actions that didn't change anything.
//
// The project name is not part of the undoable content: it belongs to the
// project (rename, save as), and undoing a box move must not bring back an
// old name.
class History {
public:
    // At most `limit` steps and about `maxBytes` of snapshots; the oldest
    // steps are dropped first (the latest one is always kept).
    explicit History(size_t limit = 200, size_t maxBytes = 64u << 20) : limit_(limit), maxBytes_(maxBytes) {}

    std::string snapshot(const Canvas& canvas) const;
    bool record(const std::string& before, const Canvas& canvas, Msg label = Msg::StepEdit);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo(Canvas& canvas);
    bool redo(Canvas& canvas);
    void clear();

    // Puts the canvas back to `snapshot` without recording anything (used
    // to cancel a drag in progress).
    void rollback(Canvas& canvas, const std::string& snapshot) const;

    // What the last undo()/redo() did, and the boxes it touched (present
    // after the step; removed ones are left out).
    Msg lastLabel() const { return lastLabel_; }
    const std::set<std::string>& lastChanged() const { return lastChanged_; }

    size_t undoDepth() const { return undo_.size(); }
    size_t bytes() const { return bytes_; }
    // Changes on every record/undo/redo/clear: lets a caller check that
    // nothing happened in between two points.
    uint64_t version() const { return version_; }

private:
    struct Step {
        std::string snapshot;
        Msg label;
    };
    size_t limit_;
    size_t maxBytes_;
    size_t bytes_ = 0;
    uint64_t version_ = 0;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    Msg lastLabel_ = Msg::StepEdit;
    std::set<std::string> lastChanged_;

    bool travel(Canvas& canvas, std::vector<Step>& from, std::vector<Step>& to);
    void trim();
};

}  // namespace ic

#endif  // IC_HISTORY_H
