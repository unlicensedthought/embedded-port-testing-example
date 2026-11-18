// master_core.h - the master band's state machine, free of any hardware.
//
// Owns run state authoritatively. Produces messages and motor output from
// button events, inbound messages and the passage of time. All I/O goes
// through a BandIO.
#pragma once
#include <stdint.h>
#include "protocol.h"
#include "timing.h"
#include "band_io.h"

namespace pp {

class MasterCore {
 public:
  explicit MasterCore(BandIO& io) : io_(io) {}

  // session is this boot's epoch id (a random number on hardware). The slave
  // adopts the master's state wholesale whenever it sees a new session, which
  // is how a master reboot resyncs while the link stays up.
  void begin(uint32_t session) {
    session_ = session;
    lastDriftSync_ = io_.now();
  }

  // Local button HOLD: flip the authoritative run state.
  void onHold() { commitState(!running_); }

  // One inbound message from the paired slave.
  void onMessage(const Msg& m) {
    switch (m.type) {
      case MSG_POLL:
        sendCommand();  // answer with the current truth
        break;
      case MSG_ACK:
        if (awaitingAck_ && m.seq == seq_ && (m.running != 0) == running_) {
          awaitingAck_ = false;  // slave confirmed the exact state
          retries_ = 0;
        }
        break;
      case MSG_REQUEST_TOGGLE:
        commitState(!running_);  // the slave only asks; the master decides
        break;
      default:
        break;
    }
  }

  // Call every loop: drives retries, drift sync and the motor.
  void tick() {
    const uint32_t now = io_.now();

    if (awaitingAck_ && now >= ackDeadline_) {
      if (retries_ < MAX_RETRIES) {
        retries_++;
        sendCommand();
        ackDeadline_ = now + ACK_TIMEOUT_MS;
      } else {
        forceOff();  // slave never confirmed -> both OFF
      }
    }

    if (running_ && (now - lastDriftSync_) >= DRIFT_SYNC_MS) {
      Msg m{};
      m.type = MSG_SYNC_DRIFT;
      m.session = session_;
      m.seq = seq_;
      m.running = 1;
      m.cycle_pos = now % DEFAULT_PATTERN.cycle_ms;
      io_.send(m);
      lastDriftSync_ = now;
    }

    io_.motor(running_ && masterActive(DEFAULT_PATTERN, now));
  }

  bool     running()     const { return running_; }
  uint32_t seq()         const { return seq_; }
  bool     awaitingAck() const { return awaitingAck_; }

 private:
  void sendCommand() {
    Msg m{};
    m.type = MSG_COMMAND;
    m.session = session_;
    m.seq = seq_;
    m.running = running_ ? 1 : 0;
    m.cycle_pos = io_.now() % DEFAULT_PATTERN.cycle_ms;
    io_.send(m);
  }

  void commitState(bool want) {
    running_ = want;
    seq_++;
    awaitingAck_ = true;
    retries_ = 0;
    sendCommand();
    ackDeadline_ = io_.now() + ACK_TIMEOUT_MS;
  }

  void forceOff() {
    running_ = false;
    awaitingAck_ = false;
    seq_++;
    for (uint8_t i = 0; i < MAX_RETRIES; i++) sendCommand();  // best-effort blast
    io_.motor(false);
    io_.log("master: sync failed, forced OFF");
  }

  BandIO&  io_;
  bool     running_       = false;
  uint32_t seq_           = 0;
  bool     awaitingAck_   = false;
  uint32_t ackDeadline_   = 0;
  uint8_t  retries_       = 0;
  uint32_t lastDriftSync_ = 0;
  uint32_t session_       = 0;
};

}  // namespace pp
