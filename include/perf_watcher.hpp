// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "event_config.hpp"

#include <string>

#include <linux/perf_event.h>
#include <stdint.h>

enum class PerfWatcherUseKernel {
  kOff = 0,  // always off
  kRequired, // always on
  kTry,      // On if possible, default to OFF on failure
};

struct PerfWatcherOptions {
  PerfWatcherUseKernel use_kernel;
  bool is_freq;
  uint8_t nb_frames_to_skip; // number of bottom frames to skip in stack trace
                             // (useful for allocation profiling to remove
                             // frames belonging to lib_ddprofiling.so)
};

struct PerfWatcher {
  int ddprof_event_type; // ddprof event type from DDPROF_EVENT_NAMES enum
  std::string desc;
  uint64_t sample_type; // perf sample type: specifies values included in sample
  int type; // perf event type (software / hardware / tracepoint / ... or custom
            // for non-perf events)
  unsigned long config; // specifies which perf event is requested
  union {
    int64_t sample_period;
    uint64_t sample_frequency;
  };
  int sample_type_id; // index into the sample types defined in this header
  uint16_t sample_stack_size; // size of the stack to capture

  // perf_event_open configs
  struct PerfWatcherOptions options;
  // tracepoint configuration
  EventConfValueSource value_source; // how to normalize the sample value
  uint8_t regno;
  uint8_t raw_off;
  uint8_t raw_sz;
  double value_scale;
  std::string tracepoint_event;
  std::string tracepoint_group;
  std::string tracepoint_label;
  // Other configs
  bool suppress_pid;
  bool suppress_tid;
  int pprof_sample_idx;       // index into the SampleType in the pprof
  int pprof_count_sample_idx; // index into the pprof for the count
  EventConfMode output_mode;  // defines how sample data is aggregated
};

// The Datadog backend only understands pre-configured event types.  Those
// types are defined here, and then referenced in the watcher
// The last column is a dependent type which is always aggregated as a count
// whenever the main type is aggregated.
#define PROFILE_TYPE_TABLE(X)                                                  \
  X(NOCOUNT, "nocount", nocount, NOCOUNT)                                      \
  X(TRACEPOINT, "tracepoint", events, NOCOUNT)                                 \
  X(CPU_NANOS, "cpu-time", nanoseconds, CPU_SAMPLE)                            \
  X(CPU_SAMPLE, "cpu-samples", count, NOCOUNT)                                 \
  X(ALLOC_SAMPLE, "alloc-samples", count, NOCOUNT)                             \
  X(ALLOC_SPACE, "alloc-space", bytes, ALLOC_SAMPLE)

#define X_ENUM(a, b, c, d) DDPROF_PWT_##a,
typedef enum DDPROF_SAMPLE_TYPES {
  PROFILE_TYPE_TABLE(X_ENUM) DDPROF_PWT_LENGTH,
} DDPROF_SAMPLE_TYPES;
#undef X_ENUM

// Define our own event type on top of perf event types
enum DDProfTypeId { kDDPROF_TYPE_CUSTOM = PERF_TYPE_MAX + 100 };

enum DDProfCustomCountId { kDDPROF_COUNT_ALLOCATIONS = 0 };

// Kernel events are necessary to get a full accounting of CPU
// This depend on the state of configuration (capabilities /
// perf_event_paranoid) Attempt to activate them and remove them if you fail
#define IS_FREQ_TRY_KERNEL                                                     \
  { .use_kernel = PerfWatcherUseKernel::kTry, .is_freq = true }

#define IS_FREQ                                                                \
  { .is_freq = true }

#define USE_KERNEL                                                             \
  { .use_kernel = PerfWatcherUseKernel::kRequired }

#ifdef DDPROF_OPTIM
#  define NB_FRAMES_TO_SKIP 4
#else
#  define NB_FRAMES_TO_SKIP 5
#endif

#define SKIP_FRAMES                                                            \
  { .nb_frames_to_skip = NB_FRAMES_TO_SKIP }

// Whereas tracepoints are dynamically configured and can be checked at runtime,
// we lack the ability to inspect events of type other than TYPE_TRACEPOINT.
// Accordingly, we maintain a list of events, even though the type of these
// events are marked as tracepoint unless they represent a well-known profiling
// type!
// clang-format off
//  short    desc               perf event type      perf event count type                  period/freq   profile sample type     addtl. configs
// cppcheck-suppress preprocessorErrorDirective
#define EVENT_CONFIG_TABLE(X) \
  X(hCPU,    "CPU Cycles",      PERF_TYPE_HARDWARE,  PERF_COUNT_HW_CPU_CYCLES,              99,           DDPROF_PWT_TRACEPOINT,  IS_FREQ)                 \
  X(hREF,    "Ref. CPU Cycles", PERF_TYPE_HARDWARE,  PERF_COUNT_HW_REF_CPU_CYCLES,          1000,         DDPROF_PWT_TRACEPOINT,  IS_FREQ)                 \
  X(hINST,   "Instr. Count",    PERF_TYPE_HARDWARE,  PERF_COUNT_HW_INSTRUCTIONS,            1000,         DDPROF_PWT_TRACEPOINT,  IS_FREQ)                 \
  X(hCREF,   "Cache Ref.",      PERF_TYPE_HARDWARE,  PERF_COUNT_HW_CACHE_REFERENCES,        999,          DDPROF_PWT_TRACEPOINT,  {})                      \
  X(hCMISS,  "Cache Miss",      PERF_TYPE_HARDWARE,  PERF_COUNT_HW_CACHE_MISSES,            999,          DDPROF_PWT_TRACEPOINT,  {})                      \
  X(hBRANCH, "Branche Instr.",  PERF_TYPE_HARDWARE,  PERF_COUNT_HW_BRANCH_INSTRUCTIONS,     999,          DDPROF_PWT_TRACEPOINT,  {})                      \
  X(hBMISS,  "Branch Miss",     PERF_TYPE_HARDWARE,  PERF_COUNT_HW_BRANCH_MISSES,           999,          DDPROF_PWT_TRACEPOINT,  {})                      \
  X(hBUS,    "Bus Cycles",      PERF_TYPE_HARDWARE,  PERF_COUNT_HW_BUS_CYCLES,              1000,         DDPROF_PWT_TRACEPOINT,  IS_FREQ)                 \
  X(hBSTF,   "Bus Stalls(F)",   PERF_TYPE_HARDWARE,  PERF_COUNT_HW_STALLED_CYCLES_FRONTEND, 1000,         DDPROF_PWT_TRACEPOINT,  IS_FREQ)                 \
  X(hBSTB,   "Bus Stalls(B)",   PERF_TYPE_HARDWARE,  PERF_COUNT_HW_STALLED_CYCLES_BACKEND,  1000,         DDPROF_PWT_TRACEPOINT,  IS_FREQ)                 \
  X(sCPU,    "CPU Time",        PERF_TYPE_SOFTWARE,  PERF_COUNT_SW_TASK_CLOCK,              99,           DDPROF_PWT_CPU_NANOS,   IS_FREQ_TRY_KERNEL)      \
  X(sPF,     "Page Faults",     PERF_TYPE_SOFTWARE,  PERF_COUNT_SW_PAGE_FAULTS,             1,            DDPROF_PWT_TRACEPOINT,  USE_KERNEL)              \
  X(sCS,     "Con. Switch",     PERF_TYPE_SOFTWARE,  PERF_COUNT_SW_CONTEXT_SWITCHES,        1,            DDPROF_PWT_TRACEPOINT,  USE_KERNEL)              \
  X(sMig,    "CPU Migrations",  PERF_TYPE_SOFTWARE,  PERF_COUNT_SW_CPU_MIGRATIONS,          99,           DDPROF_PWT_TRACEPOINT,  IS_FREQ)                 \
  X(sPFMAJ,  "Minor Faults",    PERF_TYPE_SOFTWARE,  PERF_COUNT_SW_PAGE_FAULTS_MIN,         99,           DDPROF_PWT_TRACEPOINT,  USE_KERNEL)              \
  X(sPFMIN,  "Major Faults",    PERF_TYPE_SOFTWARE,  PERF_COUNT_SW_PAGE_FAULTS_MAJ,         99,           DDPROF_PWT_TRACEPOINT,  USE_KERNEL)              \
  X(sALGN,   "Align. Faults",   PERF_TYPE_SOFTWARE,  PERF_COUNT_SW_ALIGNMENT_FAULTS,        99,           DDPROF_PWT_TRACEPOINT,  IS_FREQ)                 \
  X(sEMU,    "Emu. Faults",     PERF_TYPE_SOFTWARE,  PERF_COUNT_SW_EMULATION_FAULTS,        99,           DDPROF_PWT_TRACEPOINT,  IS_FREQ)                 \
  X(sDUM,    "Dummy",           PERF_TYPE_SOFTWARE,  PERF_COUNT_SW_DUMMY,                   1,            DDPROF_PWT_NOCOUNT,     {})                      \
  X(sALLOC,  "Allocations",     kDDPROF_TYPE_CUSTOM, kDDPROF_COUNT_ALLOCATIONS,             524288,       DDPROF_PWT_ALLOC_SPACE, SKIP_FRAMES)
// clang-format on

#define X_ENUM(a, b, c, d, e, f, g) DDPROF_PWE_##a,
typedef enum DDPROF_EVENT_NAMES {
  DDPROF_PWE_TRACEPOINT = -1,
  EVENT_CONFIG_TABLE(X_ENUM) DDPROF_PWE_LENGTH,
} DDPROF_EVENT_NAMES;
#undef X_ENUM

// Helper functions for event-type watcher lookups
const PerfWatcher *ewatcher_from_idx(int idx);
const PerfWatcher *ewatcher_from_str(const char *str);
const PerfWatcher *tracepoint_default_watcher();
bool watcher_has_countable_sample_type(const PerfWatcher *watcher);
bool watcher_has_tracepoint(const PerfWatcher *watcher);
int watcher_to_count_sample_type_id(const PerfWatcher *watcher);
const char *event_type_name_from_idx(int idx);

// Helper functions for sample types
const char *sample_type_name_from_idx(int idx);
const char *sample_type_unit_from_idx(int idx);
int sample_type_id_to_count_sample_type_id(int idx);

// Helper functions, mostly for tests
uint64_t perf_event_default_sample_type();
