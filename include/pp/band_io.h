// band_io.h - the port. Everything a band core needs from the outside world.
//
// The cores never call millis(), esp_now_send() or digitalWrite(). They call
// these three things. On the board an adapter implements them with the real
// calls. On a laptop a test implements them with a virtual clock, a lossy
// radio and a recorded motor trace. Same core, different adapter.
#pragma once
#include <stdint.h>
#include "protocol.h"

namespace pp {

struct BandIO {
  virtual uint32_t now() = 0;           // monotonic milliseconds
  virtual void send(const Msg& m) = 0;  // transmit one message to the peer
  virtual void motor(bool on) = 0;      // drive the motor on or off
  virtual void log(const char*) {}      // optional diagnostics, no-op default
  virtual ~BandIO() = default;
};

}  // namespace pp
