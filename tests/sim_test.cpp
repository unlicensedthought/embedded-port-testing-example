// sim_test.cpp - end-to-end scenarios: the REAL cores, a virtual clock, a lossy
// radio. Seeds are fixed, so a run is repeatable.
//
// build: c++ -std=c++17 -I include -I sim tests/sim_test.cpp -o /tmp/pp_sim && /tmp/pp_sim
#include "check.h"
#include "sim_world.h"

using namespace sim;

// Run `ms` steps and count the steps where both motors were on at once.
static int runCountingOverlaps(World& w, uint32_t ms, bool* sawBoth = nullptr) {
  int overlaps = 0;
  for (uint32_t i = 0; i < ms; i++) {
    w.step();
    if (w.masterIO.motorOn() && w.slaveIO.motorOn()) overlaps++;
    if (sawBoth && w.master->running() && w.slave->running()) *sawBoth = true;
  }
  return overlaps;
}

// After settling, check every ms of `ms`: the master's motor matches the
// predicate at the master's clock position, and the slave's matches it one radio
// latency behind (the slave aligns to a position stamped before the radio
// carried it, so it runs `latency` ms late).
static bool exactAlternation(World& w, uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    w.step();
    const uint32_t pos = w.t - 1;  // the ms that was just ticked
    if (w.masterIO.motorOn() != masterActive(DEFAULT_PATTERN, pos)) return false;
    if (w.slaveIO.motorOn() != slaveActive(DEFAULT_PATTERN, pos - w.radio.latency)) return false;
  }
  return true;
}

int main() {
  std::printf("A: start from the master, steady alternation\n");
  {
    World w(1);
    w.run(500);
    w.master->onHold();
    w.run(300);
    CHECK(w.master->running() && w.slave->running(), "both bands running");
    CHECK(exactAlternation(w, 3000), "exact alternation over 3 s");
  }

  std::printf("B: stop\n");
  {
    World w(2);
    w.master->onHold(); w.run(300);
    w.master->onHold(); w.run(300);
    CHECK(!w.master->running() && !w.slave->running(), "both bands stopped");
    bool quiet = true;
    for (int i = 0; i < 1000; i++) { w.step(); if (w.masterIO.motorOn() || w.slaveIO.motorOn()) quiet = false; }
    CHECK(quiet, "both motors silent for a full second");
  }

  std::printf("C: start from the SLAVE button (request, master decides)\n");
  {
    World w(3);
    w.run(500);
    w.slave->onHold();
    w.run(500);
    CHECK(w.master->running() && w.slave->running(), "slave button started both");
    CHECK(exactAlternation(w, 2000), "alternation holds after a slave-initiated start");
  }

  std::printf("D: 30%% packet loss both ways\n");
  {
    World w(4);
    w.radio.m2sLoss = 0.30; w.radio.s2mLoss = 0.30;
    w.run(500);
    w.master->onHold();
    w.run(5000);
    CHECK(w.master->running() && w.slave->running(), "state converged under 30% loss");
    CHECK(exactAlternation(w, 4000), "alternation clean once running (timing is local)");
  }

  std::printf("E: every ACK lost -> the master forces itself OFF\n");
  {
    World w(5);
    w.run(500);
    w.radio.s2mUp = false;  // slave->master link dead: no ACKs, no polls
    w.master->onHold();
    w.run(1000);
    CHECK(!w.master->running(), "master forced OFF after retries");
    CHECK(!w.slave->running(), "slave also OFF (it got the forced-off command)");
    bool logged = false;
    for (const auto& l : w.masterIO.logs) if (l.find("forced OFF") != std::string::npos) logged = true;
    CHECK(logged, "master logged the fail-safe");
  }

  std::printf("F: link lost while running -> the slave watchdog trips\n");
  {
    World w(6);
    w.run(500);
    w.master->onHold(); w.run(500);
    CHECK(w.slave->running(), "slave running before the link dies");
    w.radio.m2sUp = false; w.radio.s2mUp = false;
    w.run(SLAVE_WATCHDOG_MS + 500);
    CHECK(!w.slave->running(), "slave forced OFF by the watchdog");
    CHECK(w.master->running(), "master keeps running (it is the authority)");
  }

  std::printf("G: master reboot with the link up -> the slave resyncs\n");
  {
    World w(7);
    w.run(500);
    w.master->onHold(); w.run(500);
    w.master->onHold(); w.run(300);  // seq is now 2 on both
    w.master->onHold(); w.run(300);  // running again, seq 3
    CHECK(w.master->running() && w.slave->running(), "both running before the reboot");
    w.rebootMaster();                // new session, seq restarts at 0
    w.run(1000);
    CHECK(!w.master->running(), "rebooted master starts OFF");
    CHECK(!w.slave->running(), "slave adopted the rebooted master's state despite its lower seq");
    w.master->onHold(); w.run(500);
    CHECK(w.slave->running(), "and follows the new master from then on");
  }

  std::printf("H: 50 ppm slave crystal drift over 46 min\n");
  {
    World w(11, 50.0);
    w.run(500);
    w.master->onHold(); w.run(500);
    bool sawBoth = false;
    const int overlaps = runCountingOverlaps(w, 46u * 60u * 1000u, &sawBoth);
    CHECK(sawBoth, "both bands stayed running (the overlap check is not vacuous)");
    CHECK(overlaps == 0, "no ms with both motors on, guard + drift sync hold");
    CHECK(w.masterIO.syncDriftSent >= 3, "master sent at least 3 drift-sync packets");
  }

  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL SCENARIOS PASS\n", failures);
  return failures ? 1 : 0;
}
