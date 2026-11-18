// protocol.h - wire format and tunables both bands agree on. Pure data.
#pragma once
#include <stdint.h>

namespace pp {

enum MsgType : uint8_t {
  MSG_POLL           = 1,  // slave -> master: "any pending state for me?"
  MSG_COMMAND        = 2,  // master -> slave: authoritative state to apply
  MSG_ACK            = 3,  // slave -> master: confirms the exact applied state
  MSG_REQUEST_TOGGLE = 4,  // slave -> master: the slave's button asks for a toggle
  MSG_SYNC_DRIFT     = 5,  // master -> slave: phase correction
};

#pragma pack(push, 1)
struct Msg {
  uint8_t  type;       // one of MsgType
  uint32_t session;    // master's boot id; changes when the master restarts
  uint32_t seq;        // increments on every master-issued state change
  uint8_t  running;    // 0 = off, 1 = on
  uint32_t cycle_pos;  // master's position in the cycle, for phase alignment
};
#pragma pack(pop)

constexpr uint8_t  MAX_RETRIES            = 5;
constexpr uint32_t ACK_TIMEOUT_MS         = 40;
constexpr uint32_t SLAVE_WATCHDOG_MS      = 5000;  // no master contact -> force OFF
constexpr uint32_t DRIFT_SYNC_MS          = 15UL * 60UL * 1000UL;
constexpr uint32_t SLAVE_POLL_INTERVAL_MS = 150;

}  // namespace pp
