// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbolizer.hpp"

#include "ddog_profiling_utils.hpp" // for write_location_blaze
#include "ddres.hpp"
#include "demangler/demangler.hpp"
#include "logger.hpp"

#include <cassert>

namespace ddprof {
inline void write_location_no_sym(ElfAddress_t ip, const MapInfo &mapinfo,
                                  ddog_prof_Location *ffi_location) {
  write_mapping(mapinfo, &ffi_location->mapping);
  // write empty with empty function name, to enable remote symbolization
  write_function({}, mapinfo._sopath, &ffi_location->function);
  ffi_location->address = ip;
}

int Symbolizer::remove_unvisited() {
  // Remove all unvisited blaze_symbolizer instances from the map
  const auto count = std::erase_if(_symbolizer_map, [](const auto &item) {
    auto const &[key, blaze_symbolizer_wrapper] = item;
    return !blaze_symbolizer_wrapper._visited;
  });
  return count;
}

void Symbolizer::reset_unvisited_flag() {
  // Reset visited flag for the remaining entries
  for (auto &item : _symbolizer_map) {
    item.second._visited = false;
  }
}

DDRes Symbolizer::symbolize_pprof(std::span<ElfAddress_t> elf_addrs,
                                  std::span<ProcessAddress_t> process_addrs,
                                  FileInfoId_t file_id,
                                  const std::string &elf_src,
                                  const MapInfo &map_info,
                                  std::span<ddog_prof_Location> locations,
                                  unsigned &write_index,
                                  BlazeResultsWrapper &results) {
  blaze_symbolizer *symbolizer = nullptr;
  if (elf_addrs.size() != process_addrs.size()) {
    LG_WRN("Error in provided addresses when symbolizing pprofs");
    return ddres_warn(DD_WHAT_PPROF); // or some other error handling
  }
  if (elf_addrs.empty() || elf_src.empty()) {
    return ddres_warn(DD_WHAT_PPROF); // or some other error handling
  }
  ddprof::HeterogeneousLookupStringMap<std::string> *demangled_names = nullptr;
  bool retry_symbolization = true;
  bool use_debug = true;
  const blaze_result *blaze_res = nullptr;
  while (retry_symbolization && !_disable_symbolization) {
    const auto it = _symbolizer_map.find(file_id);
    const char *resolved_src = elf_src.c_str();
    // This is to avoid we change the path at every call (for different pids)
    // The cache takes into account the first path given
    if (it != _symbolizer_map.end()) {
      resolved_src = it->second._elf_src.c_str();
      symbolizer = it->second._symbolizer.get();
      it->second._visited = true;
      demangled_names = &(it->second._demangled_names);
      retry_symbolization = false;
      use_debug = it->second._use_debug;
    } else {
      auto pair = _symbolizer_map.emplace(
          file_id,
          BlazeSymbolizerWrapper(elf_src, inlined_functions, use_debug));
      DDPROF_DCHECK_FATAL(pair.second, "Unable to insert symbolizer object");
      symbolizer = pair.first->second._symbolizer.get();
      demangled_names = &(pair.first->second._demangled_names);
    }
    // Initialize the src_elf structure
    const blaze_symbolize_src_elf src_elf{
        .type_size = sizeof(blaze_symbolize_src_elf),
        .path = resolved_src,
        .debug_syms = use_debug,
        .reserved = {},
    };

    // Symbolize the addresses
    blaze_res = blaze_symbolize_elf_virt_offsets(
        symbolizer, &src_elf, elf_addrs.data(), elf_addrs.size());
    if (blaze_res) {
      DDPROF_DCHECK_FATAL(blaze_res->cnt == elf_addrs.size(),
                          "Symbolizer: Mismatch between size of returned "
                          "symbols and size of given elf addresses");
      results.blaze_results.push_back(blaze_res);
      // Demangling cache based on stability of unordered map
      // This will be moved to the backend
      for (size_t i = 0; i < blaze_res->cnt && i < elf_addrs.size(); ++i) {
        const blaze_sym *cur_sym = blaze_res->syms + i;
        // Update the location
        DDRES_CHECK_FWD(write_location_blaze(
            _reported_addr_format == k_elf ? elf_addrs[i] : process_addrs[i],
            (*demangled_names), map_info, *cur_sym, write_index, locations));
      }
    } else if (retry_symbolization && use_debug) {
      LG_NTC("Unable to symbolize with debug symbols, retrying for %s",
             elf_src.c_str());
      _symbolizer_map.erase(file_id);
      use_debug = false;
      continue;
    }
    retry_symbolization = false;
  }
  // Handle the case of no blaze result
  // This can happen when file descriptors are exhausted
  // OR symbolization is disabled
  if (!blaze_res) {
    if (_reported_addr_format == k_elf) {
      for (auto el : elf_addrs) {
        write_location_no_sym(el, map_info, &locations[write_index++]);
      }
    } else {
      for (auto el : process_addrs) {
        write_location_no_sym(el, map_info, &locations[write_index++]);
      }
    }
  }
  return {};
}
} // namespace ddprof
