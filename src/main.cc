#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "bench/engine.h"
#include "common/stats.h"

namespace {

std::uint64_t Number(std::string_view value, std::uint64_t scale = 1) {
  std::uint64_t number = 0;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), number);
  const auto limit = std::numeric_limits<std::int64_t>::max();
  if (error != std::errc{} || end != value.data() + value.size() ||
      number > limit / scale) {
    throw std::invalid_argument("invalid or overflowing numeric value");
  }
  return number * scale;
}

std::uint64_t Bytes(std::string_view value) {
  if (value.ends_with('k') || value.ends_with('K')) {
    return Number(value.substr(0, value.size() - 1), 1024);
  }
  if (value.ends_with('m') || value.ends_with('M')) {
    return Number(value.substr(0, value.size() - 1), 1024ULL * 1024);
  }
  if (value.ends_with('g') || value.ends_with('G')) {
    return Number(value.substr(0, value.size() - 1), 1024ULL * 1024 * 1024);
  }
  return Number(value);
}

std::chrono::nanoseconds Duration(std::string_view value) {
  std::uint64_t scale = 1'000'000'000;
  if (value.ends_with("ms")) {
    scale = 1'000'000;
    value.remove_suffix(2);
  } else if (value.ends_with('s')) {
    value.remove_suffix(1);
  }
  return std::chrono::nanoseconds(Number(value, scale));
}

void Print(const iobench::Config& c, const iobench::Result& result, bool csv) {
  std::vector<double> latencies;
  latencies.reserve(result.samples.size());
  for (const auto& sample : result.samples) {
    latencies.push_back(static_cast<double>(sample.latency_ns) / 1000.0);
  }
  const auto elapsed = std::chrono::nanoseconds(result.elapsed_ns);
  const auto stats =
      iobench::Summarize(std::move(latencies), c.block_size, elapsed);
  const auto op = c.operation == iobench::Operation::Read ? "read" : "write";
  const auto pattern =
      c.pattern == iobench::Pattern::Sequential ? "seq" : "rand";
  std::cout << std::fixed << std::setprecision(6);
  if (csv) {
    std::cout << "schema_version,record_type,engine,iodepth,op,block_size,file_"
                 "size,pattern,"
                 "warmup_ns,seq,offset,latency_ns,operations,bytes,elapsed_ns,"
                 "mib_per_s,iops,min_us,mean_us,p50_us,p95_us,p99_us,max_us,"
                 "submitted,completed,max_inflight\n";
    const auto prefix = [&](std::string_view type) {
      std::cout << "2," << type << ',' << c.engine << ',' << c.iodepth << ','
                << op << ',' << c.block_size << ',' << c.file_size << ','
                << pattern << ',' << c.warmup.count() << ',';
    };
    for (std::size_t i = 0; i < result.samples.size(); ++i) {
      const auto& sample = result.samples[i];
      prefix("sample");
      std::cout << i << ',' << sample.offset << ',' << sample.latency_ns
                << ",,,,,,,,,,,,,,\n";
    }
    prefix("summary");
    std::cout << ",,," << result.samples.size() << ','
              << result.samples.size() * c.block_size << ','
              << result.elapsed_ns << ',' << stats.mib_per_second << ','
              << stats.iops << ',' << stats.min_us << ',' << stats.mean_us
              << ',' << stats.p50_us << ',' << stats.p95_us << ','
              << stats.p99_us << ',' << stats.max_us << ',' << result.submitted
              << ',' << result.completed << ',' << result.max_inflight << '\n';
    return;
  }
  std::cout
      << "No fsync; fio agreement depends on workload and host.\n"
      << "engine=" << c.engine << " iodepth=" << c.iodepth << '\n'
      << "op=" << op << " pattern=" << pattern << " block_size=" << c.block_size
      << " file_size=" << c.file_size << " warmup_ns=" << c.warmup.count()
      << '\n'
      << "submitted=" << result.submitted << " completed=" << result.completed
      << " max_inflight=" << result.max_inflight << '\n'
      << "operations=" << result.samples.size()
      << " elapsed_s=" << std::chrono::duration<double>(elapsed).count()
      << " MiB/s=" << stats.mib_per_second << " IOPS=" << stats.iops << '\n'
      << "latency_us  min       mean      p50       p95       p99       max\n"
      << "            " << stats.min_us << "  " << stats.mean_us << "  "
      << stats.p50_us << "  " << stats.p95_us << "  " << stats.p99_us << "  "
      << stats.max_us << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    iobench::Config config;
    bool csv = false;
    for (int i = 1; i < argc; ++i) {
      const std::string_view flag = argv[i];
      if (flag == "--help") {
        std::cout << R"(Usage: iobench --file PATH [options]
  --engine sync|odirect|iouring  default: sync; iouring uses O_DIRECT
  --iodepth N              default: 1; sync/odirect require 1
  --pattern seq|rand       default: seq
  --op read|write          default: read
  --block-size SIZE        bytes or binary k/m/g suffix; default: 4k
  --file-size SIZE         positive block multiple; default: 1g
  --duration TIME          integer seconds or ms suffix; default: 5s
  --ops N                  positive measured operation count; overrides duration
  --warmup TIME            discarded time-based phase; default: 1s; 0 disables
  --output table|csv       default: table
WARNING: use disposable regular files only. Writes overwrite their contents.
Read setup creates/extends files and populates new bytes before timing.
One userspace thread; O_DIRECT needs a supported block-backed filesystem.
No durability; see docs/validation.md for fio comparison scope.
)";
        return 0;
      }
      if (i + 1 == argc) {
        throw std::invalid_argument("missing option value");
      }
      const std::string_view value = argv[++i];
      if (flag == "--file") {
        config.file = value;
      } else if (flag == "--engine" && (value == "sync" || value == "odirect" ||
                                        value == "iouring")) {
        config.engine = value;
      } else if (flag == "--iodepth") {
        const auto depth = Number(value);
        if (depth == 0 || depth > std::numeric_limits<unsigned>::max()) {
          throw std::invalid_argument(
              "--iodepth must fit a positive unsigned integer");
        }
        config.iodepth = static_cast<unsigned>(depth);
      } else if (flag == "--pattern" && (value == "seq" || value == "rand")) {
        config.pattern = value == "seq" ? iobench::Pattern::Sequential
                                        : iobench::Pattern::Random;
      } else if (flag == "--op" && (value == "read" || value == "write")) {
        config.operation = value == "read" ? iobench::Operation::Read
                                           : iobench::Operation::Write;
      } else if (flag == "--block-size") {
        config.block_size = Bytes(value);
      } else if (flag == "--file-size") {
        config.file_size = Bytes(value);
      } else if (flag == "--duration") {
        config.duration = Duration(value);
      } else if (flag == "--ops") {
        config.operations = Number(value);
        if (config.operations == 0) {
          throw std::invalid_argument("--ops must be positive");
        }
      } else if (flag == "--warmup") {
        config.warmup = Duration(value);
      } else if (flag == "--output" && (value == "csv" || value == "table")) {
        csv = value == "csv";
      } else {
        throw std::invalid_argument("unknown option or invalid value");
      }
    }
    Print(config, iobench::Engine(config).Run(), csv);
  } catch (const std::exception& error) {
    std::cerr << "iobench: " << error.what() << '\n';
    return 2;
  }
}
