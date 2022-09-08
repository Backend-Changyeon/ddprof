// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_symbol.hpp"

#include <gtest/gtest.h>
#include <string>

namespace ddprof {

TEST(SymbolTest, compute_elf_range_offset) {
  /*
    clang-format off
    Example extracted from these traces
    <DEBUG>Oct 17 07:46:11 ddprof[587]: Looking for : e06 = (560b979b6e06 - 560b979b6000) / (offset : 2000) / dso:/usr/local/bin/BadBoggleSolver_run
    <INFORMATIONAL>Oct 17 07:46:11 ddprof[587]: WO VMA lsym.from=26b0, lsym.to=2f68 (bias=560b979b4000) symname=_ZN3tng4mainEiPPc
    <INFORMATIONAL>Oct 17 07:46:11 ddprof[587]: DWFL: WARNING -- YEAH IN NORMALIZED RANGE
    <DEBUG>Oct 17 07:46:11 ddprof[587]: Insert: 6b0,f6f -> _ZN3tng4mainEiPPc,0,8 / shndx=16
    clang-format on
  */
  ElfAddress_t file_pc = 0x2e06;
  GElf_Sym elf_sym = {.st_name = 0,
                      .st_info = '\0',
                      .st_other = '\0',
                      .st_shndx = 0,
                      .st_value = 0x26b0,
                      .st_size = 0x8b8};
  ElfAddress_t start_sym;
  ElfAddress_t end_sym;
  bool res = compute_elf_range(file_pc, elf_sym, start_sym, end_sym);
  EXPECT_TRUE(res);
  EXPECT_EQ(start_sym, 0x26b0);
  EXPECT_EQ(end_sym, 0x2f67);
}

} // namespace ddprof
