// mac_guard.h - compile-time guard: the build fails while a peer MAC is still
// the unedited placeholder {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00}.
//
// Usage, right after the MAC array definition:
//
//   static uint8_t PEER_MAC[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00};
//   PP_ASSERT_MAC_NOT_PLACEHOLDER(PEER_MAC);
//
// It is a static_assert, so the failure is a compile error on the laptop, not a
// radio that silently talks to nobody. Needs a constant-expression-readable
// array (a constexpr or const array).
#pragma once
#include <stdint.h>

#define PP_ASSERT_MAC_NOT_PLACEHOLDER(sym)                                       \
  static_assert(                                                                 \
      !((sym)[0] == 0xDE && (sym)[1] == 0xAD && (sym)[2] == 0xBE &&              \
        (sym)[3] == 0xEF && (sym)[4] == 0x00 && (sym)[5] == 0x00),               \
      "build refused: peer MAC is still the unedited placeholder "               \
      "{0xDE,0xAD,0xBE,0xEF,0x00,0x00}; set the real board MAC before flashing.")
