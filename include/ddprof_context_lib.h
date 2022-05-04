// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once
#include "ddres_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DDProfInput DDProfInput;
typedef struct DDProfContext DDProfContext;
typedef struct PerfWatcher PerfWatcher;

/***************************** Context Management *****************************/
DDRes ddprof_context_set(DDProfInput *input, DDProfContext *);
void ddprof_context_free(DDProfContext *);

int ddprof_context_allocation_profiling_watcher_idx(const DDProfContext *ctx);

#ifdef __cplusplus
}
#endif