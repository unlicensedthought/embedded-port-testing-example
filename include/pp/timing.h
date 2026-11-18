// timing.h - the alternation predicates, shared by both bands and the tests.
//
// Given a cycle position in ms, say whether the master or the slave should be
// vibrating. Pure integer math, no hardware, so the host test runs the exact
// code the firmware runs.
//
// The bands split the cycle into non-overlapping slots with a guard gap:
//   master active on [0,              on_ms)
//   slave  active on [on_ms+GUARD_MS, 2*on_ms + GUARD_MS)
#pragma once
#include <stdint.h>

namespace pp {

struct Pattern {
  uint32_t cycle_ms;  // full period
  uint32_t on_ms;     // active slot width per band
};

// Gap between the two slots so a fast crystal can't make the bands overlap.
constexpr uint32_t GUARD_MS = 50;

// The one pattern in this example: 1000 ms cycle, 300 ms on per band.
constexpr Pattern DEFAULT_PATTERN = {1000, 300};

// Invariant: both slots and the guard fit in the cycle.
constexpr bool patternFits(const Pattern& p) {
  return 2 * p.on_ms + GUARD_MS <= p.cycle_ms;
}
static_assert(patternFits(DEFAULT_PATTERN), "default pattern must fit its cycle");

inline bool masterActive(const Pattern& p, uint32_t pos) {
  return (pos % p.cycle_ms) < p.on_ms;
}

inline bool slaveActive(const Pattern& p, uint32_t pos) {
  const uint32_t c = pos % p.cycle_ms;
  const uint32_t start = p.on_ms + GUARD_MS;
  return c >= start && c < start + p.on_ms;
}

}  // namespace pp
