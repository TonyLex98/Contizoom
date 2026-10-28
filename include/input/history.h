#ifndef IC_HISTORY_H
#define IC_HISTORY_H

#include <cstddef>
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
class History {
public:
    explicit History(size_t limit = 200) : limit_(limit) {}

    std::string snapshot(const Canvas& canvas) const;
    bool record(const std::string& before, const Canvas& canvas);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo(Canvas& canvas);
    bool redo(Canvas& canvas);
    void clear();

    size_t undoDepth() const { return undo_.size(); }

private:
    size_t limit_;
    std::vector<std::string> undo_;
    std::vector<std::string> redo_;

    void restore(Canvas& canvas, const std::string& snapshot) const;
};

}  // namespace ic

#endif  // IC_HISTORY_H
