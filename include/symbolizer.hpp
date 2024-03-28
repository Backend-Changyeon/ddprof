// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include "datadog/blazesym.h"
#include "ddprof_defs.hpp"
#include "ddprof_file_info-i.hpp"
#include "ddres_def.hpp"
#include "map_utils.hpp"
#include "mapinfo_table.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct ddog_prof_Location;

namespace ddprof {
class Symbolizer {
public:
  enum AddrFormat {
    k_elf,
    k_process,
  };

  explicit Symbolizer(bool inlined_functions = false,
                      bool disable_symbolization = false,
                      AddrFormat reported_addr_format = k_process)
      : inlined_functions(inlined_functions),
        _disable_symbolization(disable_symbolization),
        _reported_addr_format(reported_addr_format) {}

  struct BlazeResultsWrapper {
    BlazeResultsWrapper() = default;
    ~BlazeResultsWrapper() {
      for (auto &result : blaze_results) {
        blaze_result_free(result);
      }
    }

    // Delete copy constructor and copy assignment operator
    BlazeResultsWrapper(const BlazeResultsWrapper &) = delete;
    BlazeResultsWrapper &operator=(const BlazeResultsWrapper &) = delete;

    // Optionally define move constructor and move assignment operator
    BlazeResultsWrapper(BlazeResultsWrapper &&other) noexcept
        : blaze_results(std::move(other.blaze_results)) {
      other.blaze_results.clear();
    }

    BlazeResultsWrapper &operator=(BlazeResultsWrapper &&other) noexcept {
      if (this != &other) {
        for (auto &result : blaze_results) {
          blaze_result_free(result);
        }
        blaze_results = std::move(other.blaze_results);
        other.blaze_results.clear();
      }
      return *this;
    }

    std::vector<const blaze_result *> blaze_results{};
  };

  /// Fills the locations at the write index using address and elf source.
  /// assumption is that all addresses are from this source file
  /// Parameters
  /// addrs - Elf address
  /// process_addrs - Process address (only used for pprof reporting)
  /// file_id - a way to identify this file in a unique way
  /// elf_src - a path to the source file (idealy stable)
  /// map_info - the mapping information to write to the pprof
  /// locations - the output pprof strucure
  /// write_index - input / output parameter updated based on what is written
  /// results - A handle object for lifetime of strings.
  ///          Should be kept until interned strings are no longer needed.
  DDRes symbolize_pprof(std::span<ElfAddress_t> addrs,
                        std::span<ProcessAddress_t> process_addrs,
                        FileInfoId_t file_id, const std::string &elf_src,
                        const MapInfo &map_info,
                        std::span<ddog_prof_Location> locations,
                        unsigned &write_index, BlazeResultsWrapper &results);
  static void free_session_results(BlazeResultsWrapper &results) {
    for (auto &result : results.blaze_results) {
      blaze_result_free(result);
      result = nullptr;
    }
  }
  int remove_unvisited();
  void reset_unvisited_flag();

private:
  struct BlazeSymbolizerDeleter {
    void operator()(blaze_symbolizer *ptr) const {
      if (ptr != nullptr) {
        blaze_symbolizer_free(ptr);
      }
    }
  };

  struct BlazeSymbolizerWrapper {
    static blaze_symbolizer_opts create_opts_with_debug(bool inlined_fns) {
      return blaze_symbolizer_opts{.type_size = sizeof(blaze_symbolizer_opts),
                                   .auto_reload = false,
                                   .code_info = true,
                                   .inlined_fns = inlined_fns,
                                   .demangle = false,
                                   .reserved = {}};
    }
    static blaze_symbolizer_opts create_opts_no_debug() {
      return blaze_symbolizer_opts{.type_size = sizeof(blaze_symbolizer_opts),
                                   .auto_reload = false,
                                   .code_info = false,
                                   .inlined_fns = false,
                                   .demangle = false,
                                   .reserved = {}};
    }
    explicit BlazeSymbolizerWrapper(std::string elf_src, bool inlined_fns,
                                    bool use_debug = true)
        : opts(use_debug ? create_opts_with_debug(inlined_fns)
                         : create_opts_no_debug()),
          _symbolizer(blaze_symbolizer_new_opts(&opts)),
          _elf_src(std::move(elf_src)), _use_debug(use_debug) {}
    blaze_symbolizer_opts opts;
    std::unique_ptr<blaze_symbolizer, BlazeSymbolizerDeleter> _symbolizer;
    ddprof::HeterogeneousLookupStringMap<std::string> _demangled_names;
    std::string _elf_src;
    bool _visited{true};
    bool _use_debug;
  };

  std::unordered_map<FileInfoId_t, BlazeSymbolizerWrapper> _symbolizer_map;
  bool inlined_functions;
  bool _disable_symbolization;
  AddrFormat _reported_addr_format;
};
} // namespace ddprof
