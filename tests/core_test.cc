#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "bench/engine.h"
#include "bench/io.h"
#include "common/offsets.h"
#include "common/stats.h"

TEST(Stats, EmptyAndSingleSample) {
  const auto second = std::chrono::seconds(1);
  EXPECT_DOUBLE_EQ(iobench::Summarize({}, 4096, second).p99_us, 0);
  const auto single = iobench::Summarize({7}, 4096, second);
  EXPECT_DOUBLE_EQ(single.min_us, 7);
  EXPECT_DOUBLE_EQ(single.mean_us, 7);
  EXPECT_DOUBLE_EQ(single.p50_us, 7);
  EXPECT_DOUBLE_EQ(single.p99_us, 7);
  EXPECT_DOUBLE_EQ(single.max_us, 7);
}

TEST(Stats, UnsortedPercentilesAndRates) {
  const auto stats =
      iobench::Summarize({9, 1, 7, 3}, 4096, std::chrono::seconds(2));
  EXPECT_DOUBLE_EQ(stats.min_us, 1);
  EXPECT_DOUBLE_EQ(stats.mean_us, 5);
  EXPECT_DOUBLE_EQ(stats.p50_us, 5);
  EXPECT_NEAR(stats.p95_us, 8.7, 1e-12);
  EXPECT_NEAR(stats.p99_us, 8.94, 1e-12);
  EXPECT_DOUBLE_EQ(stats.max_us, 9);
  EXPECT_DOUBLE_EQ(stats.iops, 2);
  EXPECT_DOUBLE_EQ(stats.mib_per_second, 0.0078125);
  EXPECT_THROW(iobench::Summarize({1}, 4096, std::chrono::seconds(0)),
               std::invalid_argument);
}

TEST(Offsets, SequentialWrapAndRandomBounds) {
  iobench::Offsets sequential(4ULL * 4096, 4096, iobench::Pattern::Sequential);
  for (std::uint64_t i = 0; i < 12; ++i) {
    EXPECT_EQ(sequential.Next(), (i % 4) * 4096);
  }
  iobench::Offsets random(4ULL * 4096, 4096, iobench::Pattern::Random);
  std::array<bool, 4> seen{};
  for (int i = 0; i < 1000; ++i) {
    const auto offset = random.Next();
    EXPECT_EQ(offset % 4096, 0);
    ASSERT_LT(offset, 4ULL * 4096);
    seen[offset / 4096] = true;
  }
  for (bool hit : seen) {
    EXPECT_TRUE(hit);
  }
  EXPECT_THROW((iobench::Offsets(4096, 0, iobench::Pattern::Sequential)),
               std::invalid_argument);
  EXPECT_THROW((iobench::Offsets(4097, 4096, iobench::Pattern::Sequential)),
               std::invalid_argument);
}

TEST(Engine, TinyWriteReadPreservesKnownBytes) {
  std::string path =
      (std::filesystem::temp_directory_path() / "iobench-XXXXXX").string();
  const int fd = mkstemp(path.data());
  ASSERT_GE(fd, 0);
  close(fd);
  struct Cleanup {
    std::string path;
    ~Cleanup() { std::filesystem::remove(path); }
  } cleanup{path};

  iobench::Config config;
  config.file = path;
  config.file_size = 4ULL * 4096;
  config.operations = 4;
  config.warmup = std::chrono::milliseconds(1);
  config.operation = iobench::Operation::Write;
  const auto write = iobench::Engine(config).Run();
  ASSERT_EQ(write.samples.size(), 4);
  EXPECT_GT(write.elapsed_ns, 0);
  config.operation = iobench::Operation::Read;
  const auto read = iobench::Engine(config).Run();
  ASSERT_EQ(read.samples.size(), 4);
  EXPECT_GT(read.elapsed_ns, 0);
  std::vector<double> latencies;
  for (const auto& sample : read.samples) {
    EXPECT_GE(sample.latency_ns, 0);
    latencies.push_back(static_cast<double>(sample.latency_ns) / 1000.0);
  }
  const auto stats =
      iobench::Summarize(std::move(latencies), config.block_size,
                         std::chrono::nanoseconds(read.elapsed_ns));
  EXPECT_GT(stats.iops, 0);
  EXPECT_LE(stats.min_us, stats.p99_us);
  EXPECT_LE(stats.p99_us, stats.max_us);
  std::ifstream input(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(input)), {});
  EXPECT_EQ(bytes, std::string(config.file_size, '\x5a'));
}

TEST(DirectIo, MisalignmentFailsBeforeIssuingIo) {
  alignas(4096) std::array<char, 8192> buffer{};
  const iobench::Alignment alignment{4096, 4096, 4096};
  EXPECT_NO_THROW(iobench::CheckAlignment(buffer.data(), 0, 4096, alignment));
  // An invalid fd would yield EBADF if the guard let any request reach pread.
  EXPECT_THROW(iobench::Transfer(-1, std::span(buffer.data() + 1, 4096), 0,
                                 false, alignment),
               std::invalid_argument);
  EXPECT_THROW(iobench::Transfer(-1, std::span(buffer.data(), 4096), 1, false,
                                 alignment),
               std::invalid_argument);
  EXPECT_THROW(iobench::Transfer(-1, std::span(buffer.data(), 4095), 0, false,
                                 alignment),
               std::invalid_argument);
}

TEST(IoUring, SubmissionsAndCompletionsMatchRequestedOperations) {
  std::string path =
      (std::filesystem::temp_directory_path() / "iobench-XXXXXX").string();
  const int fd = mkstemp(path.data());
  ASSERT_GE(fd, 0);
  close(fd);
  struct Cleanup {
    std::string path;
    ~Cleanup() { std::filesystem::remove(path); }
  } cleanup{path};

  iobench::Config config;
  config.file = path;
  config.engine = "iouring";
  config.iodepth = 32;
  config.file_size = 4ULL * 1024 * 1024;
  config.warmup = std::chrono::milliseconds(1);
  for (const std::uint64_t count : {7, 77}) {
    config.operations = count;
    const auto result = iobench::Engine(config).Run();
    EXPECT_EQ(result.samples.size(), count);
    EXPECT_EQ(result.submitted, count);
    EXPECT_EQ(result.completed, count);
    EXPECT_EQ(result.max_inflight, std::min<std::uint64_t>(count, 32));
  }
}
