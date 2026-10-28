#ifndef IC_PROCESS_H
#define IC_PROCESS_H

namespace ic::process {

// Id of the running process.
unsigned long currentId();

// Whether a process with this id is still running (used to tell a live
// project lock from one left behind by a crash).
bool isAlive(unsigned long pid);

}  // namespace ic::process

#endif  // IC_PROCESS_H
