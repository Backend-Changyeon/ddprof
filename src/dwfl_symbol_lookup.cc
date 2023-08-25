// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_symbol_lookup.hpp"

#include "ddprof_module.hpp"
#include "dwfl_hdr.hpp"
#include "dwfl_internals.hpp"
#include "dwfl_symbol.hpp"
#include "logger.hpp"
#include "string_format.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>

namespace ddprof {

DwflSymbolLookup::DwflSymbolLookup() : _lookup_setting(K_CACHE_ON) {
  if (const char *env_p = std::getenv("DDPROF_CACHE_SETTING")) {
    if (strcmp(env_p, "VALIDATE") == 0) {
      // Allows to compare the accuracy of the cache
      _lookup_setting = K_CACHE_VALIDATE;
      LG_NTC("%s : Validate the cache data at every call", __FUNCTION__);
    } else {
      LG_WRN("%s : ignoring DDPROF_CACHE_SETTING value %s", __FUNCTION__,
             env_p);
    }
  }
}

unsigned DwflSymbolLookup::size() const {
  unsigned total_nb_elts = 0;
  std::for_each(
      _file_info_map.begin(), _file_info_map.end(),
      [&](FileInfo2SymbolVT const &el) { total_nb_elts += el.second.size(); });
  return total_nb_elts;
}

/****************/
/* Range implem */
/****************/

// Retrieve existing symbol or attempt to read from dwarf
SymbolIdx_t DwflSymbolLookup::get_or_insert(const DDProfMod &ddprof_mod,
                                            SymbolTable &table,
                                            DsoSymbolLookup &dso_symbol_lookup,
                                            FileInfoId_t file_info_id,
                                            ProcessAddress_t process_pc,
                                            const Dso &dso) {
  ++_stats._calls;
  ElfAddress_t elf_pc = process_pc - ddprof_mod._sym_bias;

#ifdef DEBUG
  LG_DBG("Looking for : %lx = (%lx - %lx) / dso:%s", elf_pc, process_pc,
         ddprof_mod._low_addr, dso._filename.c_str());
#endif
  SymbolMap &map = _file_info_map[file_info_id];
  SymbolMap::FindRes find_res = map.find_closest(elf_pc);
  if (find_res.second) { // already found the correct symbol
#ifdef DEBUG
    LG_DBG("Match : %lx,%lx -> %s,%d", find_res.first->first,
           find_res.first->second.get_end(),
           table[find_res.first->second.get_symbol_idx()]._symname.c_str(),
           find_res.first->second.get_symbol_idx());
#endif
    // cache validation mechanism: force dwfl lookup to compare with matched
    // symbols
    if (_lookup_setting == K_CACHE_VALIDATE) {
      if (symbol_lookup_check(ddprof_mod._mod, process_pc,
                              table[find_res.first->second.get_symbol_idx()])) {
        ++_stats._errors;
      }
    }
    ++_stats._hit;
    return find_res.first->second.get_symbol_idx();
  }

  return insert(ddprof_mod, table, dso_symbol_lookup, process_pc, dso, map);
}

SymbolIdx_t DwflSymbolLookup::insert(const DDProfMod &ddprof_mod,
                                     SymbolTable &table,
                                     DsoSymbolLookup &dso_symbol_lookup,
                                     ProcessAddress_t process_pc,
                                     const Dso &dso, SymbolMap &map) {

  Symbol symbol;
  GElf_Sym elf_sym;
  Offset_t lbias;

  ElfAddress_t elf_pc = process_pc - ddprof_mod._sym_bias;

  if (!symbol_get_from_dwfl(ddprof_mod._mod, process_pc, symbol, elf_sym,
                            lbias)) {
    ++_stats._no_dwfl_symbols;
    // Override with info from dso
    // Avoid bouncing on these requests and insert an element
    Offset_t start_sym = elf_pc;
    Offset_t end_sym = start_sym + 1; // minimum range
// #define ADD_ADDR_IN_SYMB // creates more elements (but adds info on
// addresses)
#ifdef ADD_ADDR_IN_SYMB
    // adds interesting debug information that can be used to investigate
    // symbolization failures. Also causes memory increase
    SymbolIdx_t symbol_idx =
        dso_symbol_lookup.get_or_insert(elf_pc, dso, table);
#else
    SymbolIdx_t symbol_idx = dso_symbol_lookup.get_or_insert(dso, table);
#endif
#ifdef DEBUG
    LG_NTC("Insert (dwfl failure): %lx,%lx -> %s,%d,%s", start_sym, end_sym,
           table[symbol_idx]._symname.c_str(), symbol_idx,
           dso.to_string().c_str());
#endif
    map.emplace(start_sym, SymbolSpan(end_sym, symbol_idx));
    return symbol_idx;
  }

  if (lbias != ddprof_mod._sym_bias) {
    LG_NTC("Failed (PID%d) assumption %s - %lx != %lx", dso._pid,
           dso._filename.c_str(), lbias, ddprof_mod._sym_bias);
    assert(0);
  }

  {
    ElfAddress_t start_sym;
    ElfAddress_t end_sym;
    // All paths bellow will insert symbol in the table
    SymbolIdx_t symbol_idx = table.size();
    table.push_back(std::move(symbol));

    Symbol &sym_ref = table.back();
    if (sym_ref._srcpath.empty()) {
      // override with info from dso (this slightly mixes mappings and sources)
      // But it helps a lot at Datadog (as mappings are ignored for now in UI)
      sym_ref._srcpath = dso.format_filename();
    }

    if (!compute_elf_range(elf_pc, elf_sym, start_sym, end_sym)) {
      // elf section does not add up to something that makes sense
      // insert this PC without considering elf section
      start_sym = elf_pc;
      end_sym = elf_pc;
#ifdef DEBUG
      LG_DBG("elf_range failure --> Insert: %lx,%lx -> %s,%d / shndx=%d",
             start_sym, end_sym, sym_ref._symname.c_str(), symbol_idx,
             elf_sym.st_shndx);
#endif
      map.emplace(start_sym, SymbolSpan(end_sym, symbol_idx));
      return symbol_idx;
    }

#ifdef DEBUG
    LG_DBG("Insert: %lx,%lx -> %s,%d / shndx=%d", start_sym, end_sym,
           sym_ref._symname.c_str(), symbol_idx, elf_sym.st_shndx);
#endif
    map.emplace(start_sym, SymbolSpan(end_sym, symbol_idx));
    return symbol_idx;
  }
}

bool DwflSymbolLookup::symbol_lookup_check(Dwfl_Module *mod,
                                           Dwarf_Addr process_pc,
                                           const Symbol &symbol) {
  GElf_Off loffset;
  GElf_Sym lsym;
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;

  const char *localsymname = dwfl_module_addrinfo(
      mod, process_pc, &loffset, &lsym, &lshndxp, &lelfp, &lbias);

#ifdef DEBUG
  LG_DBG("DWFL: Lookup res = %lx->%lx, shndx=%u, biais=%lx, elfp=%p, "
         "shndxp=%u, %s",
         lsym.st_value, lsym.st_value + lsym.st_size, lsym.st_shndx, lbias,
         lelfp, lshndxp, localsymname);
#endif

  bool error_found = false;
  if (!localsymname) { // symbol failure no use checking
    return error_found;
  }

  if (symbol._symname.empty()) {
    LG_ERR("Error from cache : non null symname = %s", localsymname);
  } else {
    if (strcmp(symbol._symname.c_str(), localsymname) != 0) {
      LG_ERR("Error from cache symname Real=%s vs Cache=%s ", localsymname,
             symbol._symname.c_str());
      error_found = true;
    }
    if (error_found) {
      LG_ERR("symname = %s\n", symbol._symname.c_str());
    }
  }
  return error_found;
}

void DwflSymbolLookupStats::display(unsigned nb_elts) const {
  static const int k_cent_precision = 10000;
  if (_calls) {
    LG_NTC("DWFL_SYMB | %10s | [%d/%d] = %ld", "Hit", _hit, _calls,
           (static_cast<int64_t>(_hit) * k_cent_precision) / _calls);
    if (_errors) {
      LG_WRN("DWFL_SYMB | %10s | [%d/%d] = %ld", "Errors", _errors, _calls,
             (static_cast<int64_t>(_errors) * k_cent_precision) / _calls);
    }
    if (_no_dwfl_symbols) {
      LG_NTC("DWFL_SYMB | %10s | [%d/%d] = %d", "Not found", _no_dwfl_symbols,
             _calls, (_no_dwfl_symbols * k_cent_precision) / _calls);
    }
    LG_NTC("DWFL_SYMB | %10s | %d", "Size ", nb_elts);
  } else {
    LG_NTC("DWFL_SYMB NO CALLS");
  }
}

void DwflSymbolLookupStats::reset() {
  _hit = 0;
  _calls = 0;
  _errors = 0;
}

} // namespace ddprof
