// logic_test.cpp - unit tests for the pure logic, with a stub BandIO.
//
// build: c++ -std=c++17 -I include tests/logic_test.cpp -o /tmp/pp_logic && /tmp/pp_logic
#include <cstdint>
#include <vector>
#include "check.h"
#include "pp/protocol.h"
#include "pp/timing.h"
#include "pp/seq.h"
#include "pp/band_io.h"
#include "pp/master_core.h"
#include "pp/slave_core.h"

using namespace pp;

// The smallest possible adapter: a settable clock and a log of what was sent.
struct StubIO : public BandIO {
  uint32_t t = 0;
  bool motorState = false;
  std::vector<Msg> sent;
  uint32_t now() override { return t; }
  void send(const Msg& m) override { sent.push_back(m); }
  void motor(bool on) override { motorState = on; }

  const Msg* lastOfType(MsgType type) const {
    for (auto it = sent.rbegin(); it != sent.rend(); ++it)
      if (it->type == type) return &*it;
    return nullptr;
  }
  int count(MsgType type) const {
    int n = 0;
    for (const auto& m : sent) n += (m.type == type);
    return n;
  }
};

static Msg command(uint32_t session, uint32_t seq, uint8_t running, uint32_t pos = 0) {
  Msg m{};
  m.type = MSG_COMMAND;
  m.session = session;
  m.seq = seq;
  m.running = running;
  m.cycle_pos = pos;
  return m;
}

int main() {
  std::printf("wire format\n");
  CHECK(sizeof(Msg) == 14, "Msg is exactly 14 bytes (packed)");

  std::printf("timing\n");
  const Pattern& p = DEFAULT_PATTERN;
  CHECK(patternFits(p), "default pattern fits its cycle with the guard");
  CHECK(!patternFits(Pattern{1000, 480}), "a pattern whose slots + guard exceed the cycle is rejected");
  {
    bool exclusive = true, masterSeen = false, slaveSeen = false;
    uint32_t guardMs = 0;
    for (uint32_t pos = 0; pos < 5 * p.cycle_ms; pos++) {
      const bool m = masterActive(p, pos), s = slaveActive(p, pos);
      if (m && s) exclusive = false;
      if (m) masterSeen = true;
      if (s) slaveSeen = true;
      if (!m && !s && pos < p.cycle_ms && pos >= p.on_ms && pos < p.on_ms + GUARD_MS) guardMs++;
    }
    CHECK(exclusive, "master and slave are never active in the same ms over 5 cycles");
    CHECK(masterSeen && slaveSeen, "both bands do get a slot");
    CHECK(guardMs == GUARD_MS, "the guard gap after the master slot is silent for GUARD_MS");
  }
  CHECK(masterActive(p, 0) && masterActive(p, 299) && !masterActive(p, 300), "master slot is [0,300)");
  CHECK(!slaveActive(p, 349) && slaveActive(p, 350) && slaveActive(p, 649) && !slaveActive(p, 650),
        "slave slot is [350,650)");
  CHECK(masterActive(p, 7 * p.cycle_ms + 10), "predicates wrap on the cycle");

  std::printf("seq acceptance\n");
  CHECK(seqIsNewer(2, 1), "next seq is newer");
  CHECK(!seqIsNewer(1, 1), "equal seq is not newer");
  CHECK(!seqIsNewer(1, 2), "older seq is not newer");
  CHECK(seqIsNewer(0, 0xFFFFFFFFu), "rollover: 0 follows UINT32_MAX");
  CHECK(seqIsNewer(5, 0xFFFFFFFEu), "rollover: small value after a near-max one");
  CHECK(!seqIsNewer(0xFFFFFFFFu, 0), "a value just behind current (across zero) is old");
  CHECK(!seqIsNewer(1u << 30, 1), "garbage far-future seq is rejected");
  CHECK(seqIsNewer(1 + MAX_SEQ_JUMP - 1, 1), "largest believable jump is accepted");
  CHECK(!seqIsNewer(1 + MAX_SEQ_JUMP, 1), "one past the largest believable jump is rejected");

  std::printf("master core\n");
  {
    StubIO io; MasterCore m(io); m.begin(7);
    m.onHold();
    CHECK(m.running() && m.seq() == 1 && m.awaitingAck(), "hold starts, bumps seq, waits for ACK");
    const Msg* c = io.lastOfType(MSG_COMMAND);
    CHECK(c && c->running == 1 && c->session == 7 && c->seq == 1, "COMMAND carries the new truth");

    Msg wrongAck{}; wrongAck.type = MSG_ACK; wrongAck.seq = 1; wrongAck.running = 0;
    m.onMessage(wrongAck);
    CHECK(m.awaitingAck(), "an ACK for the wrong state is ignored");
    Msg ack{}; ack.type = MSG_ACK; ack.seq = 1; ack.running = 1;
    m.onMessage(ack);
    CHECK(!m.awaitingAck(), "the exact ACK clears the wait");

    io.t = 0; m.tick();
    CHECK(io.motorState, "motor on inside the master slot");
    io.t = 400; m.tick();
    CHECK(!io.motorState, "motor off outside the master slot");
  }
  {
    StubIO io; MasterCore m(io); m.begin(1);
    m.onHold();
    const int before = io.count(MSG_COMMAND);
    io.t = ACK_TIMEOUT_MS; m.tick();
    CHECK(io.count(MSG_COMMAND) == before + 1, "a missing ACK triggers a retry");
    for (int i = 0; i < 20; i++) { io.t += ACK_TIMEOUT_MS; m.tick(); }
    CHECK(!m.running() && !m.awaitingAck(), "after MAX_RETRIES the master forces OFF");
    CHECK(!io.motorState, "and the motor is off");
  }
  {
    StubIO io; MasterCore m(io); m.begin(1);
    Msg req{}; req.type = MSG_REQUEST_TOGGLE;
    m.onMessage(req);
    CHECK(m.running(), "a slave toggle request makes the master start");
    Msg poll{}; poll.type = MSG_POLL;
    const int before = io.count(MSG_COMMAND);
    m.onMessage(poll);
    CHECK(io.count(MSG_COMMAND) == before + 1, "a POLL is answered with a COMMAND");
  }

  std::printf("slave core\n");
  {
    StubIO io; SlaveCore s(io); s.begin();
    s.onMessage(command(9, 1, 1));
    CHECK(s.running() && s.seq() == 1, "first contact adopts the master's state");
    CHECK(io.lastOfType(MSG_ACK) && io.lastOfType(MSG_ACK)->seq == 1, "and ACKs it");

    s.onMessage(command(9, 5000, 0));
    CHECK(s.running() && s.seq() == 1, "same session: a wild far-future seq is not adopted");
    const int acks = io.count(MSG_ACK);
    s.onMessage(command(9, 1, 1));
    CHECK(io.count(MSG_ACK) == acks + 1 && s.seq() == 1, "duplicate retransmit is re-ACKed, not re-applied");
    s.onMessage(command(9, 2, 0));
    CHECK(!s.running() && s.seq() == 2, "a genuinely newer seq is adopted");
    s.onMessage(command(9, 1, 1));
    CHECK(!s.running(), "an older seq is ignored");
    s.onMessage(command(10, 1, 1));
    CHECK(s.running() && s.seq() == 1, "a new session wins even with a lower seq (master reboot)");
  }
  {
    StubIO io; SlaveCore s(io); s.begin();
    s.onMessage(command(1, 1, 1, 0));
    io.t = 0; s.tick();
    CHECK(!io.motorState, "slave is quiet during the master slot");
    io.t = 400; s.tick();
    CHECK(io.motorState, "slave vibrates in its own slot");
    io.t = SLAVE_WATCHDOG_MS + 1; s.tick();
    CHECK(!s.running() && !io.motorState, "silence past the watchdog forces OFF");
    CHECK(s.seq() == 0, "and clears seq so a rebooted master can resync");
  }
  {
    StubIO io; SlaveCore s(io); s.begin();
    io.t = 1000;
    s.onMessage(command(1, 1, 1, 250));
    CHECK(s.phaseRef() == 750, "phase reference is local now minus the master's cycle position");
    Msg stray{}; stray.type = MSG_SYNC_DRIFT; stray.session = 99; stray.cycle_pos = 5;
    s.onMessage(stray);
    CHECK(s.phaseRef() == 750, "a drift sync from another session is ignored");
  }

  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
  return failures ? 1 : 0;
}
