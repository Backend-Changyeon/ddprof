// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include "ddres_def.hpp"

#include <chrono>
#include <string>

#ifdef __x86_64__
#  include <x86intrin.h>
#endif

namespace ddprof {

enum class TscState { kUninitialized, kUnavailable, kOK };
enum class TscCalibrationMethod { kAuto, kPerf, kCpuArch, kClockMonotonicRaw };

struct TscConversion {
  uint64_t offset;
  uint32_t mult;
  uint16_t shift;
  TscState state;
  TscCalibrationMethod calibration_method;
};

inline TscConversion g_tsc_conversion = {0UL, 1U, 0U, TscState::kUninitialized,
                                         TscCalibrationMethod::kAuto};

using TscCycles = uint64_t;

#ifdef __x86_64__
inline TscCycles read_tsc() { return __rdtsc(); }
#elif defined(__aarch64__)
inline TscCycles read_tsc() {
  uint64_t val;

  asm volatile("mrs %0, cntvct_el0" : "=r"(val));
  return val;
}
#else
inline TscCycles read_tsc() { return 0; }
#endif

DDRes init_tsc(TscCalibrationMethod method = TscCalibrationMethod::kAuto);

inline TscCalibrationMethod get_tsc_calibration_method() {
  return g_tsc_conversion.calibration_method;
}

inline std::string
tsc_calibration_method_to_string(TscCalibrationMethod method) {
  switch (method) {
  case TscCalibrationMethod::kClockMonotonicRaw:
    return "ClockMonotonicRaw";
  case TscCalibrationMethod::kCpuArch:
    return "CpuArch";
  case TscCalibrationMethod::kPerf:
    return "perf";
  case TscCalibrationMethod::kAuto:
    return "Auto";
  default:
    break;
  }

  return "undef";
}

inline TscState get_tsc_state() { return g_tsc_conversion.state; }

inline TscCycles get_tsc_cycles() { return read_tsc(); }

inline uint64_t tsc_cycles_to_ns(TscCycles cycles) {
  using uint128_t = unsigned __int128;
  return static_cast<uint64_t>(
             (static_cast<uint128_t>(cycles) * g_tsc_conversion.mult) >>
             g_tsc_conversion.shift) +
      g_tsc_conversion.offset;
}

inline std::chrono::nanoseconds tsc_cycles_to_duration(TscCycles cycles) {
  return std::chrono::nanoseconds{tsc_cycles_to_ns(cycles)};
}

} // namespace ddprof
