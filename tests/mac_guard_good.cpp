// A real (non-placeholder) MAC must compile. Used by scripts/verify.sh.
#include "pp/mac_guard.h"
static constexpr uint8_t PEER_MAC[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
PP_ASSERT_MAC_NOT_PLACEHOLDER(PEER_MAC);
int main() { return 0; }
