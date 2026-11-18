// The placeholder MAC must NOT compile. scripts/verify.sh expects this to fail.
#include "pp/mac_guard.h"
static constexpr uint8_t PEER_MAC[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00};
PP_ASSERT_MAC_NOT_PLACEHOLDER(PEER_MAC);
int main() { return 0; }
