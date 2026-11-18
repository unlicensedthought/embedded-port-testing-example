# embedded-port-testing-example

A small, working example of one idea: **put the logic of an embedded project behind a port, and test it on your laptop.**

The project is a hobby one. Two wristbands, each with a vibration motor, buzz in alternation (left, then right, then left) and talk to each other over ESP-NOW on ESP32-S3 boards. This repo is the hardware-free part of that firmware, trimmed down to what you need to see the pattern. It builds with any C++17 compiler. No board, no PlatformIO, no Arduino core.

It is an example of a testing pattern, not a product and not a mature test suite. It doesn't flash anything.

## The problem

Most firmware bugs live in logic: timing windows, state machines, retry counters, sequence numbers that wrap. They don't live in the peripherals. But if that logic calls `millis()`, `esp_now_send()` and `digitalWrite()` directly, you can only test it by flashing a board and watching an LED.

## The pattern

`include/pp/band_io.h` is the whole port:

```cpp
struct BandIO {
  virtual uint32_t now() = 0;           // monotonic milliseconds
  virtual void send(const Msg& m) = 0;  // transmit to the peer
  virtual void motor(bool on) = 0;      // drive the motor
  virtual void log(const char*) {}
};
```

`MasterCore` and `SlaveCore` take a `BandIO&` and never touch anything else. On the board, a thin adapter implements those calls with the real ones (sketch below). On the host, `sim/sim_world.h` implements them with a virtual clock, a lossy radio and a recorded motor. The cores are the same bytes in both places.

```
 board:  button -> adapter(millis, esp_now, GPIO) -> MasterCore / SlaveCore
 host:   test   -> SimIO(virtual clock, lossy radio, motor trace) -> the same cores
```

An illustrative board adapter (not compiled here, since it needs the Arduino core):

```cpp
struct ArduinoIO : pp::BandIO {
  uint32_t now() override { return millis(); }
  void send(const pp::Msg& m) override { esp_now_send(peerMac, (const uint8_t*)&m, sizeof m); }
  void motor(bool on) override { digitalWrite(MOTOR_PIN, on ? HIGH : LOW); }
};
```

## Quick start

```bash
./scripts/verify.sh
```

That compiles with `-Wall -Wextra -Werror`, runs both test programs, and ends with a line like `VERIFY {"verdict":"pass","gates":9}`.

Or by hand:

```bash
c++ -std=c++17 -I include tests/logic_test.cpp -o /tmp/pp_logic && /tmp/pp_logic
c++ -std=c++17 -I include -I sim tests/sim_test.cpp -o /tmp/pp_sim && /tmp/pp_sim
```

## What's in it

| Path | Job |
|---|---|
| `include/pp/band_io.h` | the port |
| `include/pp/protocol.h` | the 14-byte packed message, retry and watchdog constants |
| `include/pp/timing.h` | who vibrates when: 1000 ms cycle, 300 ms per band, a 50 ms guard gap |
| `include/pp/seq.h` | rollover-safe "is this sequence number newer" |
| `include/pp/master_core.h` | master state machine: owns run state, retries, drift sync |
| `include/pp/slave_core.h` | slave state machine: adopts state, polls, watchdog, phase lock |
| `include/pp/mac_guard.h` | compile-time refusal to build with the placeholder peer MAC |
| `sim/sim_world.h` | host adapters: virtual clock, lossy radio, crystal drift |
| `tests/logic_test.cpp` | unit tests with a short stub adapter |
| `tests/sim_test.cpp` | eight end-to-end scenarios with fixed seeds |

## How it works

- The master is the only authority on run state. The slave asks (`MSG_POLL`, `MSG_REQUEST_TOGGLE`) and confirms (`MSG_ACK`).
- A state change bumps `seq`. The master retries the `COMMAND` up to 5 times at 40 ms. If the slave never ACKs the exact state, the master forces both bands off.
- The slave accepts a command if it's from a new session (the master rebooted), or if `seqIsNewer()` says so. Equal `seq` means a duplicate, so it re-ACKs and doesn't re-apply. A far-future `seq` is rejected.
- If the slave hears nothing for 5 s it forces itself off and clears its `seq`.
- The slave aligns its phase to the master's cycle position, and the master re-sends it every 15 minutes. The 50 ms guard absorbs the drift in between.
- `mac_guard.h` is a `static_assert`. Leave the placeholder MAC in and the build stops with a message.

## What the tests cover

`scripts/verify.sh` has nine gates. Three are negative proofs. The placeholder MAC must fail to compile. A deliberately broken `seq.h` must make `logic_test` fail. And a drift-sync interval longer than the sim must make `sim_test` fail. A test that can't fail isn't telling you anything.

The sim scenarios: alternation, stop, start from the slave button, 30% packet loss both ways, total ACK loss (master forces off), link loss (slave watchdog), master reboot with the link up, and 50 ppm slave clock drift over 46 simulated minutes.

## Limits

- This is a reduced copy. The real firmware has more patterns, gesture handling and power management; they're left out here so the pattern is readable.
- The sim is a model. The radio is a coin flip with a fixed 1 ms latency. Real RF has bursts, interference and range. Passing under 30% random loss doesn't mean passing in a crowded 2.4 GHz room.
- The adapter isn't tested. The port moves the risk to the thin layer that implements it: pin numbers, ESP-NOW setup, light sleep. Only the board checks those.
- Timing is whole milliseconds on a 1 ms step. Sub-millisecond jitter isn't modeled.
- Fixed seeds make runs repeatable, but they're a handful of seeds, not a sweep.
- This is a hobby project for vibrating wristbands. It makes no health or therapeutic claims, and nothing here is medical advice.

## License

Apache-2.0. See `LICENSE`.
