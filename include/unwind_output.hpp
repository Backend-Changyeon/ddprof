// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// External API: This file should stay in C

#pragma once

#include <stdint.h>
#include <vector>

#include "ddprof_defs.hpp"

typedef struct FunLoc {
  uint64_t ip; // Relative to file, not VMA
  SymbolIdx_t _symbol_idx;
  MapInfoIdx_t _map_info_idx;

  auto operator<=>(const FunLoc &) const = default;
} FunLoc;

struct UnwindOutput {
  void clear() {
    locs.clear();
    is_incomplete = true;
  }
  std::vector<FunLoc> locs;
  int pid;
  int tid;
  bool is_incomplete;

  auto operator<=>(const UnwindOutput &) const = default;
};
