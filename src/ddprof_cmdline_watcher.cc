// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline_watcher.hpp"

#include <cassert>
#include <strings.h>
#include <utility>

#include "event_config.hpp"
#include "perf_watcher.hpp"
#include "tracepoint_config.hpp"

namespace ddprof {
namespace {
bool watcher_from_config(EventConf *conf, PerfWatcher *watcher) {
  constexpr int64_t kIgnoredWatcherID = -1L;

  // If there's no eventname, then this configuration is invalid
  if (conf->eventname.empty()) {
    return false;
  }

  // The watcher is templated; either from an existing Profiling template,
  // keyed on the eventname, or it uses the generic template for Tracepoints
  const PerfWatcher *tmp_watcher = ewatcher_from_str(conf->eventname.c_str());
  if (tmp_watcher) {
    *watcher = *tmp_watcher;
    conf->id = kIgnoredWatcherID; // matched, so invalidate Tracepoint checks
  } else if (!conf->groupname.empty()) {
    // If the event doesn't match an ewatcher, it is only valid if a group was
    // also provided (splitting events on ':' is the responsibility of the
    // parser)
    const auto *tmp_tracepoint_watcher = tracepoint_default_watcher();
    if (!tmp_tracepoint_watcher) {
      return false;
    }
    *watcher = *tmp_tracepoint_watcher;
  } else {
    return false;
  }

  if (conf->id != kIgnoredWatcherID) {
    // The most likely thing to be invalid is the selection of the tracepoint
    // from the trace events system.  If the conf has a nonzero number for the
    // id we assume the user has privileged information and knows what they
    // want. Else, we use the group/event combination to extract that id from
    // the tracefs filesystem in the canonical way.
    int64_t tracepoint_id = 0;
    if (conf->id > 0) {
      tracepoint_id = conf->id;
    } else {
      tracepoint_id = tracepoint_get_id(conf->eventname, conf->groupname);
    }
    // At this point we needed to find a valid tracepoint id
    if (tracepoint_id == kIgnoredWatcherID) {
      return false;
    }
    watcher->config = tracepoint_id;
  }

  // Configure the sampling strategy.  If no valid conf, use template default
  if (conf->cadence != 0) {
    if (conf->cad_type == EventConfCadenceType::kPeriod) {
      watcher->sample_period = conf->cadence;
    } else if (conf->cad_type == EventConfCadenceType::kFrequency) {
      watcher->sample_frequency = conf->cadence;
      watcher->options.is_freq = true;
    }
  }

  // Configure value source
  if (conf->value_source == EventConfValueSource::kRaw) {
    watcher->value_source = EventConfValueSource::kRaw;
    watcher->sample_type |= PERF_SAMPLE_RAW;
    watcher->raw_off = conf->raw_offset;
    if (conf->raw_size > 0) {
      watcher->raw_sz = conf->raw_size;
    } else {
      watcher->raw_sz = sizeof(uint64_t); // default raw entry
    }
  } else if (conf->value_source == EventConfValueSource::kRegister) {
    watcher->regno = conf->register_num;
    watcher->value_source = EventConfValueSource::kRegister;
  }

  if (conf->value_scale != 0.0) {
    watcher->value_scale = conf->value_scale;
  }
  watcher->aggregation_mode = conf->mode;
  watcher->tracepoint_event = conf->eventname;
  watcher->tracepoint_group = conf->groupname;
  watcher->tracepoint_label = conf->label;
  watcher->options.stack_sample_size = conf->stack_sample_size;
  // Allocation watcher, has an extra field to ensure we capture address

  if (watcher->config == kDDPROF_COUNT_ALLOCATIONS) {
    watcher->sample_type |= PERF_SAMPLE_ADDR;
  }

  return true;
}
} // namespace
// If this returns false, then the passed watcher should be regarded as invalid
bool watchers_from_str(const char *str, std::vector<PerfWatcher> &watchers,
                       uint32_t stack_sample_size) {
  std::vector<EventConf> configs;
  const EventConf template_conf{
      .mode = EventAggregationMode::kSum,
      .stack_sample_size = stack_sample_size,
  };
  if (EventConf_parse(str, template_conf, configs) != 0) {
    return false;
  }

  for (auto &conf : configs) {
    PerfWatcher watcher;
    if (!watcher_from_config(&conf, &watcher)) {
      return false;
    }
    watchers.push_back(std::move(watcher));
  }
  return true;
}

} // namespace ddprof
