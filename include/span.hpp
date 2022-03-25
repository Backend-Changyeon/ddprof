// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#if __cpp_lib_span

#  include <span>

namespace ddprof {
using std::as_bytes;
using std::as_writable_bytes;
using std::span;
} // namespace ddprof

#else

#  define TCB_SPAN_NAMESPACE_NAME ddprof
#  include "tcb/span.hpp"

#endif
