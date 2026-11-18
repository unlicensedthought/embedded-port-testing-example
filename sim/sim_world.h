// sim_world.h - the host-side adapters: a virtual clock, a lossy radio and a
// recorded motor, wired to the REAL MasterCore / SlaveCore.
#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "pp/band_io.h"
#include "pp/master_core.h"
#include "pp/slave_core.h"

namespace sim {
using namespace pp;

// Two directed links, each with an up/down flag and an independent loss
// probability, plus a fixed latency.
class Radio {
 public:
  explicit Radio(uint32_t seed) : rng_(seed) {}

  bool     m2sUp = true, s2mUp = true;
  double   m2sLoss = 0.0, s2mLoss = 0.0;
  uint32_t latency = 1;  // >= 1 so a send never delivers inside its own step

  void send(bool fromMaster, const Msg& m, uint32_t t) {
    if (!(fromMaster ? m2sUp : s2mUp)) return;
    const double loss = fromMaster ? m2sLoss : s2mLoss;
    if (loss > 0.0) {
      std::uniform_real_distribution<double> d(0.0, 1.0);
      if (d(rng_) < loss) return;  // dropped in flight
    }
    pending_.push_back({t + latency, fromMaster, m});
  }

  void pump(uint32_t t, std::deque<Msg>& slaveBox, std::deque<Msg>& masterBox) {
    std::vector<Item> keep;
    for (const auto& p : pending_) {
      if (p.deliverAt <= t) (p.toSlave ? slaveBox : masterBox).push_back(p.msg);
      else keep.push_back(p);
    }
    pending_.swap(keep);
  }

 private:
  struct Item { uint32_t deliverAt; bool toSlave; Msg msg; };
  std::vector<Item> pending_;
  std::mt19937 rng_;
};

// BandIO bound to the shared world clock and the radio. An optional ppm skew
// models crystal drift (the band's clock runs slightly fast or slow).
class SimIO : public BandIO {
 public:
  SimIO(bool isMaster, Radio& radio, const uint32_t& worldT, double ppm = 0.0)
      : isMaster_(isMaster), radio_(radio), worldT_(worldT), ppm_(ppm) {}

  uint32_t now() override {
    if (ppm_ == 0.0) return worldT_;
    return static_cast<uint32_t>(static_cast<double>(worldT_) * (1.0 + ppm_ / 1e6));
  }
  void send(const Msg& m) override {
    if (isMaster_ && m.type == MSG_SYNC_DRIFT) syncDriftSent++;
    radio_.send(isMaster_, m, worldT_);
  }
  void motor(bool on) override { on_ = on; }
  void log(const char* s) override { logs.push_back(s); }

  bool motorOn() const { return on_; }
  std::vector<std::string> logs;
  int syncDriftSent = 0;

 private:
  bool isMaster_;
  Radio& radio_;
  const uint32_t& worldT_;
  double ppm_;
  bool on_ = false;
};

// The whole two-band world. step() advances exactly 1 ms and mirrors the
// firmware loop: drain delivered messages, then tick each core once.
struct World {
  uint32_t t = 0;
  Radio radio;
  SimIO masterIO;
  SimIO slaveIO;
  std::deque<Msg> slaveBox, masterBox;
  std::unique_ptr<MasterCore> master;
  std::unique_ptr<SlaveCore> slave;
  uint32_t sessionCounter = 0;

  explicit World(uint32_t seed, double slavePpm = 0.0)
      : radio(seed),
        masterIO(true, radio, t, 0.0),
        slaveIO(false, radio, t, slavePpm),
        master(new MasterCore(masterIO)),
        slave(new SlaveCore(slaveIO)) {
    master->begin(++sessionCounter);
    slave->begin();
  }

  // The master loses power and comes back with a new epoch id.
  void rebootMaster() {
    master.reset(new MasterCore(masterIO));
    master->begin(++sessionCounter);
  }

  void step() {
    radio.pump(t, slaveBox, masterBox);
    while (!masterBox.empty()) { Msg m = masterBox.front(); masterBox.pop_front(); master->onMessage(m); }
    while (!slaveBox.empty())  { Msg m = slaveBox.front();  slaveBox.pop_front();  slave->onMessage(m); }
    master->tick();
    slave->tick();
    t++;
  }

  void run(uint32_t ms) { for (uint32_t i = 0; i < ms; i++) step(); }
};

}  // namespace sim
