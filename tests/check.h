// check.h - tiny CHECK macro; the test binaries print FAIL lines and a verdict.
#pragma once
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) { std::printf("  FAIL: %s\n", msg); failures++; }     \
    else         { std::printf("  ok  : %s\n", msg); }                \
  } while (0)
