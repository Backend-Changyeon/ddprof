// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.hpp"

#include <sys/types.h>

namespace ddprof {

struct UnwindState;

void unwind_init();

// Fill sample info to prepare for unwinding
void unwind_init_sample(UnwindState *us, const uint64_t *sample_regs,
                        pid_t sample_pid, uint64_t sample_size_stack,
                        const char *sample_data_stack);

// Main unwind API
DDRes unwindstate_unwind(UnwindState *us);

// Mark a cycle: garbadge collection, stats
void unwind_cycle(UnwindState *us);

// Clear unwinding structures of this pid
void unwind_pid_free(UnwindState *us, pid_t pid);

} // namespace ddprof
