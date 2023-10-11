// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddres.hpp"
#include "proc_status.hpp"

#include <sys/stat.h>
#include <sys/types.h>

namespace ddprof {

// Get internal stats from /proc/self/stat
DDRes proc_read(ProcStatus *procstat);

// check sys types for the different types (S_IFLNK, S_IFDIR...)
bool check_file_type(const char *pathname, int file_type);

// Returns false if it can not find the matching file
bool get_file_inode(const char *pathname, inode_t *inode, int64_t *size);

} // namespace ddprof
