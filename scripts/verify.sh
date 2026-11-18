#!/usr/bin/env bash
# Install/build/test in one go. Last stdout line is the verdict:
#   VERIFY {"verdict":"pass","gates":N}  or  VERIFY {"verdict":"fail","failed":"<step>"}
set -u
cd "$(dirname "$0")/.."
CXX="${CXX:-c++}"
FLAGS="-std=c++17 -Wall -Wextra -Werror"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
gates=0

fail() { echo "VERIFY {\"verdict\":\"fail\",\"failed\":\"$1\"}"; exit 1; }
gate() { # gate <name> <command...>
  local name="$1"; shift
  echo "== $name"
  if "$@"; then gates=$((gates + 1)); else fail "$name"; fi
}

# expect_fail <name> <command...>: the gate passes only if the command FAILS.
expect_fail() {
  local name="$1"; shift
  echo "== $name"
  if "$@" >/dev/null 2>&1; then fail "$name"; else gates=$((gates + 1)); fi
}

# Nothing to install: no packages, only a C++17 compiler.
gate "toolchain: $CXX present" bash -c "command -v $CXX >/dev/null && $CXX --version | head -1"

gate "build: logic_test" $CXX $FLAGS -I include tests/logic_test.cpp -o "$OUT/logic_test"
gate "test: logic_test"  "$OUT/logic_test"

gate "build: sim_test" $CXX $FLAGS -I include -I sim tests/sim_test.cpp -o "$OUT/sim_test"
gate "test: sim_test"  "$OUT/sim_test"

gate "mac_guard: a real MAC compiles" $CXX $FLAGS -I include -fsyntax-only tests/mac_guard_good.cpp
expect_fail "mac_guard: the placeholder MAC is refused" $CXX $FLAGS -I include -fsyntax-only tests/mac_guard_bad.cpp

# Negative proof for seq.h: swap in the naive comparison and the tests must fail.
mkdir -p "$OUT/mut/pp"
cp include/pp/*.h "$OUT/mut/pp/"
cat > "$OUT/mut/pp/seq.h" <<'MUT'
#pragma once
#include <stdint.h>
namespace pp {
constexpr uint32_t MAX_SEQ_JUMP = 4096;
inline bool seqIsNewer(uint32_t incoming, uint32_t current) { return incoming >= current; }
}
MUT
echo "== mutation: naive seq compare must fail logic_test"
if $CXX -std=c++17 -I "$OUT/mut" tests/logic_test.cpp -o "$OUT/logic_mut" 2>/dev/null \
   && ! "$OUT/logic_mut" >/dev/null 2>&1; then
  gates=$((gates + 1))
else
  fail "mutation: naive seq compare must fail logic_test"
fi

# Negative proof for drift sync: push the sync interval past the 46 min the sim
# runs and the drift scenario must fail.
rm -rf "$OUT/mut2"; mkdir -p "$OUT/mut2/pp"
cp include/pp/*.h "$OUT/mut2/pp/"
sed -i.bak 's/15UL \* 60UL \* 1000UL/100UL * 60UL * 1000UL/' "$OUT/mut2/pp/protocol.h" && rm -f "$OUT/mut2/pp/protocol.h.bak"
echo "== mutation: no drift sync inside 46 min must fail sim_test"
if grep -q '100UL \* 60UL' "$OUT/mut2/pp/protocol.h" \
   && $CXX -std=c++17 -I "$OUT/mut2" -I sim tests/sim_test.cpp -o "$OUT/sim_mut" 2>/dev/null \
   && ! "$OUT/sim_mut" >/dev/null 2>&1; then
  gates=$((gates + 1))
else
  fail "mutation: no drift sync inside 46 min must fail sim_test"
fi

echo "VERIFY {\"verdict\":\"pass\",\"gates\":$gates}"
