#include <gtest/gtest.h>

#include "jit/jitdump.hpp"
#include "loghandle.hpp"

namespace ddprof {

TEST(JITTest, SimpleRead) {
  LogHandle handle;
  std::string jit_path =
      std::string(UNIT_TEST_DATA) + "/" + std::string("jit-simple-julia.dump");
  JITDump jit_dump;
  DDRes res = jitdump_read(jit_path, jit_dump);
  ASSERT_TRUE(IsDDResOK(res));
  EXPECT_EQ(jit_dump.header.version, k_jit_header_version);
  EXPECT_EQ(jit_dump.code_load.size(), 13);
  EXPECT_EQ(jit_dump.debug_info.size(), 8);
}

TEST(JITTest, DotnetJITDump) {
  LogHandle handle;
  std::string jit_path = std::string(UNIT_TEST_DATA) + "/" +
      std::string("jit-dotnet-partial.dump");
  JITDump jit_dump;
  DDRes res = jitdump_read(jit_path, jit_dump);
  // File is incomplete
  ASSERT_TRUE(!IsDDResFatal(res) && !IsDDResOK(res));
  EXPECT_EQ(jit_dump.header.version, k_jit_header_version);
  EXPECT_EQ(jit_dump.code_load.size(), 8424);
  EXPECT_EQ(jit_dump.debug_info.size(), 0);
}

} // namespace ddprof
