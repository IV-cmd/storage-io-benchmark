#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <stdexcept>

#include "common/offsets.h"
#include "common/stats.h"

TEST(Stats, PercentileEdgeCases) {
  EXPECT_DOUBLE_EQ(iobench::Percentile({}, 99), 0);
  const std::array single{7.0};
  EXPECT_DOUBLE_EQ(iobench::Percentile(single, 0), 7);
  EXPECT_DOUBLE_EQ(iobench::Percentile(single, 99), 7);
  const std::array samples{9.0, 1.0, 7.0, 3.0};
  EXPECT_DOUBLE_EQ(iobench::Percentile(samples, 0), 1);
  EXPECT_DOUBLE_EQ(iobench::Percentile(samples, 50), 5);
  EXPECT_NEAR(iobench::Percentile(samples, 95), 8.7, 1e-12);
  EXPECT_NEAR(iobench::Percentile(samples, 99), 8.94, 1e-12);
  EXPECT_DOUBLE_EQ(iobench::Percentile(samples, 100), 9);
  EXPECT_DOUBLE_EQ(samples[0], 9);
  EXPECT_THROW(iobench::Percentile(samples, -1), std::invalid_argument);
  EXPECT_THROW(iobench::Percentile(samples, NAN), std::invalid_argument);
}

TEST(Stats, SummaryAndRates) {
  const std::array samples{9.0, 1.0, 7.0, 3.0};
  const auto stats = iobench::SummarizeRun(samples, 4096, 2);
  EXPECT_DOUBLE_EQ(stats.latency.min_us, 1);
  EXPECT_DOUBLE_EQ(stats.latency.mean_us, 5);
  EXPECT_DOUBLE_EQ(stats.latency.p50_us, 5);
  EXPECT_NEAR(stats.latency.p95_us, 8.7, 1e-12);
  EXPECT_NEAR(stats.latency.p99_us, 8.94, 1e-12);
  EXPECT_DOUBLE_EQ(stats.latency.max_us, 9);
  EXPECT_DOUBLE_EQ(stats.iops, 2);
  EXPECT_DOUBLE_EQ(stats.mib_per_second, 0.0078125);
  EXPECT_DOUBLE_EQ(iobench::SummarizeRun({}, 4096, 1).iops, 0);
  EXPECT_DOUBLE_EQ(iobench::SummarizeLatency({}).max_us, 0);
  EXPECT_THROW(iobench::SummarizeRun(samples, 4096, 0), std::invalid_argument);
}

TEST(Offsets, SequentialWrapAndRandomBounds) {
  iobench::Offsets sequential(4 * 4096, 4096, iobench::Pattern::Sequential);
  for (std::uint64_t i = 0; i < 12; ++i) {
    EXPECT_EQ(sequential.Next(), (i % 4) * 4096);
  }
  iobench::Offsets random(4 * 4096, 4096, iobench::Pattern::Random);
  for (int i = 0; i < 1000; ++i) {
    const auto offset = random.Next();
    EXPECT_EQ(offset % 4096, 0);
    EXPECT_LT(offset, 4 * 4096);
  }
  EXPECT_THROW((iobench::Offsets(4096, 0, iobench::Pattern::Sequential)),
               std::invalid_argument);
  EXPECT_THROW((iobench::Offsets(4097, 4096, iobench::Pattern::Sequential)),
               std::invalid_argument);
}
