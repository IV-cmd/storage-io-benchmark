#include "bench/engine.h"

#include <sys/types.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "bench/io.h"

namespace iobench {
namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

struct Pending {
  std::uint64_t offset = 0;
  Clock::time_point start;
  bool active = false;
};

Result RunPhase(Io& io, const Config& config, Nanoseconds duration,
                std::uint64_t operations, bool record) {
  Offsets offsets(config.file_size, config.block_size, config.pattern);
  std::vector<Pending> pending(io.Depth());
  Result result;
  if (record && operations != 0) {
    result.samples.reserve(operations);
  }
  const auto submitted_before = io.submitted;
  const auto completed_before = io.completed;
  std::uint64_t started = 0;
  unsigned active = 0;
  const auto start = Clock::now();
  const auto more = [&] {
    return operations != 0 ? started < operations
                           : Clock::now() - start < duration;
  };
  const auto submit = [&](unsigned slot) {
    auto& request = pending[slot];
    request.offset = offsets.Next();
    request.start = Clock::now();
    request.active = true;
    io.Submit(slot, request.offset);
    ++started;
    ++active;
    result.max_inflight = std::max(result.max_inflight, active);
  };
  for (unsigned slot = 0; slot < io.Depth() && more(); ++slot) {
    submit(slot);
  }
  io.Flush();
  while (active != 0) {
    const auto slot = io.Complete();
    const auto end = Clock::now();
    if (slot >= pending.size() || !pending[slot].active) {
      throw std::runtime_error("unknown or duplicate completion");
    }
    auto& request = pending[slot];
    if (record) {
      result.samples.push_back(
          {request.offset,
           std::chrono::duration_cast<Nanoseconds>(end - request.start).count()});
    }
    request.active = false;
    --active;
    if (more()) {
      submit(slot);
    }
    io.Flush();
  }
  result.elapsed_ns =
      std::chrono::duration_cast<Nanoseconds>(Clock::now() - start).count();
  result.submitted = io.submitted - submitted_before;
  result.completed = io.completed - completed_before;
  if (result.submitted != started || result.completed != started) {
    throw std::runtime_error("submission/completion count mismatch");
  }
  return result;
}

}  // namespace

Result Engine::Run() const {
  const auto& c = config_;
  const Offsets validate(c.file_size, c.block_size, c.pattern);
  if (c.file.empty() || c.iodepth == 0 ||
      c.file_size >
          static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) ||
      c.block_size >
          static_cast<std::uint64_t>(std::numeric_limits<ssize_t>::max()) ||
      c.warmup.count() < 0 || (c.operations == 0 && c.duration.count() <= 0)) {
    throw std::invalid_argument("invalid path, size, duration, warmup, or depth");
  }
  auto io = MakeIo(c);
  RunPhase(*io, c, c.warmup, 0, false);
  auto result = RunPhase(*io, c, c.duration, c.operations, true);
  if (result.samples.empty()) {
    throw std::runtime_error("no samples: increase duration or use --ops");
  }
  return result;
}

}  // namespace iobench
