// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "common_symbol_lookup.hpp"

namespace ddprof {

Symbol symbol_from_common(SymbolErrors lookup_case) {
  switch (lookup_case) {
  case SymbolErrors::truncated_stack:
    return Symbol(std::string(), std::string("[truncated]"), 0, std::string());
  case SymbolErrors::unknown_dso:
    return Symbol(std::string(), std::string("[unknown_dso]"), 0,
                  std::string());
  case SymbolErrors::dwfl_frame:
    return Symbol(std::string(), std::string("[dwfl_frame]"), 0, std::string());
  case SymbolErrors::incomplete_stack:
    return Symbol(std::string(), std::string("[incomplete]"), 0, std::string());
  default:
    break;
  }
  return Symbol();
}

SymbolIdx_t CommonSymbolLookup::get_or_insert(SymbolErrors lookup_case,
                                              SymbolTable &symbol_table) {
  auto const it = _map.find(lookup_case);
  SymbolIdx_t symbol_idx;
  if (it != _map.end()) {
    symbol_idx = it->second;
  } else { // insert things
    symbol_idx = symbol_table.size();
    symbol_table.push_back(symbol_from_common(lookup_case));
    _map.insert(std::pair<SymbolErrors, SymbolIdx_t>(lookup_case, symbol_idx));
  }
  return symbol_idx;
}
} // namespace ddprof