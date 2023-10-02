// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "mapinfo_lookup.hpp"

#include "ddres.hpp"

namespace ddprof {

MapInfoIdx_t MapInfoLookup::get_or_insert(pid_t pid,
                                          MapInfoTable &mapinfo_table,
                                          const Dso &dso,
                                          std::optional<BuildIdStr> build_id) {
  MapInfoAddrMap &addr_map = _mapinfo_pidmap[pid];
  auto it = addr_map.find(dso._start);

  if (it == addr_map.end()) { // create a mapinfo from dso element
    size_t const pos = dso._filename.rfind('/');
    std::string sname_str = (pos == std::string::npos)
        ? dso._filename
        : dso._filename.substr(pos + 1);
    MapInfoIdx_t const map_info_idx = mapinfo_table.size();
    mapinfo_table.emplace_back(dso._start, dso._end, dso._pgoff,
                               std::move(sname_str),
                               build_id ? *build_id : BuildIdStr{});
    addr_map.emplace(dso._start, map_info_idx);
    return map_info_idx;
  }
  return it->second;
}

} // namespace ddprof
