#include <gtest/gtest.h>
extern "C" {
#include "openair1/PHY/TOOLS/tools_defs.h"
#include "common/utils/LOG/log.h"
}
#include <cmath>

uint8_t log2_approx_ref(uint32_t x)
{
  return std::round(std::log2(x));
}

TEST(log2_approx_ref, complete)
{
  for (uint32_t i = 0; i < UINT32_MAX; i++)
    EXPECT_EQ(log2_approx(i), log2_approx_ref(i));
}

int main(int argc, char **argv)
{
  logInit();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
