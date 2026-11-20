#include "bench/engine.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>

namespace iobench {
namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

void Transfer(int fd, std::span<char> buffer, std::uint64_t offset,
              bool write) {
  std::size_t done = 0;
  while (done < buffer.size()) {
    const auto position = static_cast<off_t>(offset + done);
    const auto count =
        write ? pwrite(fd, buffer.data() + done, buffer.size() - done, position)
              : pread(fd, buffer.data() + done, buffer.size() - done, position);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::system_error(errno, std::generic_category(), "pread/pwrite");
    }
    if (count == 0) {
      throw std::runtime_error("short transfer: EOF or no write progress");
    }
    done += static_cast<std::size_t>(count);
  }
}

}  // namespace

Result Engine::Run() const {
  const auto& c = config_;
  Offsets offsets(c.file_size, c.block_size, c.pattern);
  if (c.file.empty() ||
      c.file_size >
          static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) ||
      c.block_size >
          static_cast<std::uint64_t>(std::numeric_limits<ssize_t>::max()) ||
      c.warmup.count() < 0 || (c.operations == 0 && c.duration.count() <= 0)) {
    throw std::invalid_argument("invalid path, size, duration, or warmup");
  }
  const int fd =
      open(c.file.c_str(),
           O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
  if (fd < 0) {
    throw std::system_error(errno, std::generic_category(), "open");
  }
  struct CloseFile {
    int fd;
    ~CloseFile() { close(fd); }
  } file{fd};
  struct stat info {};
  if (fstat(fd, &info) != 0) {
    throw std::system_error(errno, std::generic_category(), "fstat");
  }
  if (!S_ISREG(info.st_mode)) {
    throw std::invalid_argument("target must be a regular disposable file");
  }
  const auto allocation =
      posix_fallocate(fd, 0, static_cast<off_t>(c.file_size));
  if (allocation != 0) {
    throw std::system_error(allocation, std::generic_category(),
                            "posix_fallocate");
  }
  std::vector<char> buffer(c.block_size, '\x5a');
  if (c.operation == Operation::Read) {
    for (auto offset = static_cast<std::uint64_t>(info.st_size);
         offset < c.file_size;) {
      const auto count = std::min(c.block_size, c.file_size - offset);
      Transfer(fd, std::span(buffer).first(count), offset, true);
      offset += count;
    }
  }

  Result result;
  if (c.operations != 0) {
    result.samples.reserve(c.operations);
  }
  const auto warmup_start = Clock::now();
  while (Clock::now() - warmup_start < c.warmup) {
    Transfer(fd, buffer, offsets.Next(), c.operation == Operation::Write);
  }
  offsets = Offsets(c.file_size, c.block_size, c.pattern);
  const auto start = Clock::now();
  while (c.operations != 0 ? result.samples.size() < c.operations
                           : Clock::now() - start < c.duration) {
    const auto offset = offsets.Next();
    const auto before = Clock::now();
    Transfer(fd, buffer, offset, c.operation == Operation::Write);
    result.samples.push_back(
        {offset, std::chrono::duration_cast<Nanoseconds>(Clock::now() - before)
                     .count()});
  }
  result.elapsed_ns =
      std::chrono::duration_cast<Nanoseconds>(Clock::now() - start).count();
  if (result.samples.empty()) {
    throw std::runtime_error("no samples: increase duration or use --ops");
  }
  return result;
}

}  // namespace iobench
