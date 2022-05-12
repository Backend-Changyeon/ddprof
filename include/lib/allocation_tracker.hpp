// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.h"
#include "unlikely.h"

extern "C" {
#include "pevent.h"
}

#include <atomic>
#include <cstddef>
#include <mutex>
#include <random>

namespace ddprof {

struct RingBufferInfo;

struct TrackerThreadLocalState {
  int64_t remaining_bytes; // remaining allocation bytes until next sample
  bool remaining_bytes_initialized; // false if remaining_bytes is not
                                    // initialized

  pid_t tid; // cache of tid

  // cppcheck-suppress unusedStructMember
  bool reentry_guard; // prevent reentry in AllocationTracker (eg. when
                      // allocation are done inside AllocationTracker)
};

class AllocationTracker {
public:
  AllocationTracker(const AllocationTracker &) = delete;
  AllocationTracker &operator=(const AllocationTracker &) = delete;

  enum AllocationTrackingFlags {
    kTrackDeallocations = 0x1,
    kDeterministicSampling = 0x2
  };

  static DDRes allocation_tracking_init(uint64_t allocation_profiling_rate,
                                        uint32_t flags,
                                        const RingBufferInfo &ring_buffer);
  static void allocation_tracking_free();

  static inline __attribute__((no_sanitize("address"))) void
  track_allocation(uintptr_t addr, size_t size);
  static inline void track_deallocation(uintptr_t addr);

private:
  struct TrackerState {
    std::mutex mutex;
    std::atomic<bool> track_allocations = false;
    std::atomic<bool> track_deallocations = false;

    // cppcheck-suppress unusedStructMember
    uint64_t lost_count; // count number of lost events
    pid_t pid;           // cache of pid
  };

  AllocationTracker();
  uint64_t next_sample_interval();

  DDRes init(uint64_t mem_profile_interval, bool deterministic_sampling,
             const RingBufferInfo &ring_buffer);
  void free();

  static AllocationTracker *create_instance();

  void track_allocation(uintptr_t addr, size_t size,
                        TrackerThreadLocalState &tl_state);
  void track_deallocation(uintptr_t addr, TrackerThreadLocalState &tl_state);

  DDRes push_sample(uint64_t allocated_size, TrackerThreadLocalState &tl_state);

  TrackerState _state;
  uint64_t _sampling_interval;
  std::mt19937 _gen;
  PEvent _pevent;
  bool _deterministic_sampling;

  static thread_local TrackerThreadLocalState _tl_state;
  static AllocationTracker *_instance;
};

void AllocationTracker::track_allocation(uintptr_t addr, size_t size) {
  AllocationTracker *instance = _instance;

  // Be safe, if allocation tracker has not been initialized, just bail out
  // Also this avoids accessing TLS during startup which causes segfaults
  // with ASAN because ASAN installs its own wrapper around tls_get_addr,
  // which triggers allocations, reenters this same function and tls_get_addr
  // wrapper and wreaks havoc...
  if (!instance) {
    return;
  }

  // In shared libraries, TLS access requires a call to tls_get_addr,
  // therefore obtain a pointer on TLS state once and pass it around
  TrackerThreadLocalState &tl_state = _tl_state;

  tl_state.remaining_bytes += size;
  if (likely(tl_state.remaining_bytes < 0)) {
    return;
  }

  if (likely(
          instance->_state.track_allocations.load(std::memory_order_relaxed))) {
    instance->track_allocation(addr, size, tl_state);
  } else {
    // allocation tracking is disabled, reset state
    tl_state.remaining_bytes_initialized = false;
    tl_state.remaining_bytes = 0;
  }
}

void AllocationTracker::track_deallocation(uintptr_t) {}

} // namespace ddprof
