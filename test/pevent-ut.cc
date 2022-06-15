// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pevent_lib.hpp"

#include "ddprof_context.hpp"
#include "loghandle.hpp"
#include "perf_watcher.hpp"

#include <gtest/gtest.h>
#include <sys/sysinfo.h>
#include <unistd.h>

void mock_ddprof_context(DDProfContext *ctx) {
  ctx->num_watchers = 1;
  ctx->params.enable = true;
  ctx->watchers[0] = *ewatcher_from_str("sCPU"); // kernel-derived CPU time
}

TEST(PeventTest, setup_cleanup) {
  PEventHdr pevent_hdr;
  LogHandle log_handle;
  DDProfContext ctx = {};
  pid_t mypid = getpid();
  mock_ddprof_context(&ctx);
  pevent_init(&pevent_hdr);
  DDRes res = pevent_setup(&ctx, mypid, get_nprocs(), &pevent_hdr);
  ASSERT_TRUE(IsDDResOK(res));
  ASSERT_TRUE(pevent_hdr.size == static_cast<unsigned>(get_nprocs()));
  res = pevent_cleanup(&pevent_hdr);
  ASSERT_TRUE(IsDDResOK(res));
}
