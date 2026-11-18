// slave_core.h - the slave band's state machine, free of any hardware.
//
// Mirrors the master's authoritative state, runs the opposite half-cycle off
// its own clock (phase-locked to the master), and fails safe if the master
// goes silent.
#pragma once
#include <stdint.h>
#include "protocol.h"
#include "timing.h"
#include "seq.h"
#include "band_io.h"

namespace pp {

class SlaveCore {
 public:
  explicit SlaveCore(BandIO& io) : io_(io) {}

  void begin() { lastContact_ = io_.now(); }

  // Local button HOLD: ask the master to toggle. The master decides.
  void onHold() {
    Msg m{};
    m.type = MSG_REQUEST_TOGGLE;
    m.seq = seq_;
    io_.send(m);
  }

  void onMessage(const Msg& m) {
    switch (m.type) {
      case MSG_COMMAND:
        if (!haveSession_ || m.session != session_) {
          // First contact, or the master restarted. Its seq may be lower than
          // ours, but it is authoritative: adopt its state wholesale.
          adopt(m);
        } else if (seqIsNewer(m.seq, seq_)) {
          adopt(m);  // genuinely newer state within the same session
        } else if (m.seq == seq_) {
          sendAck();  // duplicate retransmit: re-ACK, don't re-apply
        }
        lastContact_ = io_.now();
        break;
      case MSG_SYNC_DRIFT:
        if (haveSession_ && m.session == session_) {
          alignPhase(m.cycle_pos);
          lastContact_ = io_.now();
        }
        break;
      default:
        break;
    }
  }

  // Call every loop: polls the master, runs the watchdog, drives the motor.
  void tick() {
    const uint32_t now = io_.now();

    if ((now - lastPoll_) >= SLAVE_POLL_INTERVAL_MS) {
      Msg m{};
      m.type = MSG_POLL;
      m.seq = seq_;
      io_.send(m);
      lastPoll_ = now;
    }

    // Watchdog: lost the master -> force OFF, and clear seq so a rebooted
    // master (its seq restarts low) can resync instead of being ignored.
    if ((now - lastContact_) > SLAVE_WATCHDOG_MS) {
      if (running_) {
        running_ = false;
        io_.log("slave: watchdog lost master, forced OFF");
      }
      seq_ = 0;
    }

    io_.motor(running_ && slaveActive(DEFAULT_PATTERN, now - phaseRef_));
  }

  bool     running()  const { return running_; }
  uint32_t seq()      const { return seq_; }
  uint32_t phaseRef() const { return phaseRef_; }

 private:
  void adopt(const Msg& m) {
    session_ = m.session;
    haveSession_ = true;
    seq_ = m.seq;
    running_ = (m.running != 0);
    alignPhase(m.cycle_pos);
    sendAck();
  }

  void sendAck() {
    Msg m{};
    m.type = MSG_ACK;
    m.session = session_;
    m.seq = seq_;
    m.running = running_ ? 1 : 0;
    io_.send(m);
  }

  void alignPhase(uint32_t masterPos) { phaseRef_ = io_.now() - masterPos; }

  BandIO&  io_;
  bool     running_     = false;
  uint32_t seq_         = 0;
  uint32_t phaseRef_    = 0;
  uint32_t lastPoll_    = 0;
  uint32_t lastContact_ = 0;
  uint32_t session_     = 0;
  bool     haveSession_ = false;
};

}  // namespace pp
