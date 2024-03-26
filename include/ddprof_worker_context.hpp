// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "live_allocation.hpp"
#include "pevent.hpp"
#include "proc_status.hpp"

#include <array>
#include <chrono>

namespace ddprof {

struct DDProfExporter;
struct DDProfPProf;
struct PersistentWorkerState;
struct UnwindState;
struct UserTags;
class Symbolizer;

// Mutable states within a worker
struct DDProfWorkerContext {
  // Persistent reference to the state shared accross workers
  PersistentWorkerState *persistent_worker_state{nullptr};
  PEventHdr pevent_hdr;     // perf_event buffer holder
  DDProfExporter *exp[2]{}; // wrapper around rust exporter
  DDProfPProf *pprof[2]{};  // wrapper around rust exporter
  Symbolizer *symbolizer{};
  int i_current_pprof{0};
  volatile bool exp_error{false};
  pthread_t exp_tid{0};
  UnwindState *us{};
  UserTags *user_tags{};
  ProcStatus proc_status{};
  std::chrono::steady_clock::time_point
      cycle_start_time{}; // time at which current export cycle was started
  std::chrono::steady_clock::time_point
      send_time{};          // Last time an export was sent
  uint32_t count_worker{0}; // exports since last cache clear
  std::array<uint64_t, kMaxTypeWatcher> lost_events_per_watcher{};
  LiveAllocation live_allocation;
  int64_t perfclock_offset;
  PerfClock::time_point last_processed_event_timestamp{};
};

} // namespace ddprof
