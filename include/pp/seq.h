// seq.h - rollover-safe sequence-number acceptance.
//
// Decide whether an incoming COMMAND's seq is genuinely NEWER than the state
// we hold. Two traps this avoids:
//   - a raw `incoming >= current` lets one corrupt, huge value wedge the link:
//     once `current` jumps near UINT32_MAX, every legitimate later packet is
//     smaller and gets ignored.
//   - it also strands the slave when the master reboots: the master's seq
//     restarts near 0 while the slave still holds the old large one. (The slave
//     handles that case with session ids and a watchdog; see slave_core.h.)
//
// Accept when the signed (rollover-safe) delta is positive AND within a sane
// bound, so garbage far-future values are rejected.
#pragma once
#include <stdint.h>

namespace pp {

// Largest forward jump we believe in one step. seq only moves on a state
// change (a button press), so real deltas are tiny; this bounds garbage.
constexpr uint32_t MAX_SEQ_JUMP = 4096;

inline bool seqIsNewer(uint32_t incoming, uint32_t current) {
  const int32_t delta = static_cast<int32_t>(incoming - current);  // rollover-safe
  return delta > 0 && delta < static_cast<int32_t>(MAX_SEQ_JUMP);
}

}  // namespace pp
