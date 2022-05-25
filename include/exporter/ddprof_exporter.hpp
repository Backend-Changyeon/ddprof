// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "ddprof_defs.h"
#include "ddres_def.h"
#include "exporter_input.h"
#include "perf_watcher.h"
#include "string_view.h"
}

#include "tags.hpp"

typedef struct ddprof_ffi_ProfileExporterV3 ddprof_ffi_ProfileExporterV3;
typedef struct ddprof_ffi_Profile ddprof_ffi_Profile;
typedef struct UserTags UserTags;

#define K_NB_CONSECUTIVE_ERRORS_ALLOWED 3

typedef struct DDProfExporter {
  ExporterInput _input;
  char *_url;                      // url contains path and port
  const char *_debug_pprof_prefix; // write pprofs to folder
  ddprof_ffi_ProfileExporterV3 *_exporter;
  bool _agent;
  bool _export; // debug mode : should we send profiles ?
  int32_t _nb_consecutive_errors;
} DDProfExporter;

DDRes ddprof_exporter_init(const ExporterInput *exporter_input,
                           DDProfExporter *exporter);

DDRes ddprof_exporter_new(const UserTags *user_tags, DDProfExporter *exporter);

DDRes ddprof_exporter_export(const struct ddprof_ffi_Profile *profile,
                             const ddprof::Tags &additional_tags,
                             uint32_t profile_seq, DDProfExporter *exporter);

DDRes ddprof_exporter_free(DDProfExporter *exporter);
