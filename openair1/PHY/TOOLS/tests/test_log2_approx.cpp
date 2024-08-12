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

uint8_t log2_approx64_ref(unsigned long long int x)
{
  return std::round(std::log2(static_cast<long double>(x)));
}

TEST(log2_approx, complete)
{
  for (uint32_t i = 0; i < UINT32_MAX; i++)
    EXPECT_EQ(log2_approx(i), log2_approx_ref(i));
}

TEST(log2_approx64, boundaries)
{
  for (long double i = 0; i < 64; i++) {
    unsigned long long i2 = std::pow(2.0L, i + 0.5L);
    for (unsigned long long j = -10; j <= 10; j++) {
      unsigned long long x = i2 + j;
      EXPECT_EQ(log2_approx64(x), log2_approx64_ref(x));
    }
  }
}

int main(int argc, char **argv)
{
  logInit();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
