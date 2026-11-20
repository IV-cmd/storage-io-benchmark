#include "bench/io.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace iobench {

Io::Io(const Config& config, bool direct, unsigned depth)
    : config_(config),
      fd_(open(config.file.c_str(),
               O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                   (direct ? O_DIRECT : 0),
               0600)),
      direct_(direct),
      depth_(depth) {
  if (fd_ < 0) {
    throw std::system_error(errno, std::generic_category(),
                            direct ? "open O_DIRECT" : "open");
  }
}

Io::~Io() {
  std::free(memory_);
  close(fd_);
}

void Io::Prepare() {
  struct stat info {};
  if (fstat(fd_, &info) != 0) {
    throw std::system_error(errno, std::generic_category(), "fstat");
  }
  if (!S_ISREG(info.st_mode)) {
    throw std::invalid_argument("target must be a regular disposable file");
  }
  if (direct_) {
    alignment_ = DirectAlignment(fd_);
  }
  const auto memory_alignment =
      std::max<std::size_t>(alignment_.memory, sizeof(void*));
  stride_ = (config_.block_size + memory_alignment - 1) / memory_alignment *
            memory_alignment;
  if (stride_ > std::numeric_limits<std::size_t>::max() / depth_) {
    throw std::invalid_argument("buffer allocation size overflow");
  }
  const int allocation =
      posix_memalign(&memory_, memory_alignment, stride_ * depth_);
  if (allocation != 0) {
    throw std::system_error(allocation, std::generic_category(), "posix_memalign");
  }
  CheckAlignment(memory_, 0, config_.block_size, alignment_);
  std::memset(memory_, 0x5a, stride_ * depth_);
  const int space =
      posix_fallocate(fd_, 0, static_cast<off_t>(config_.file_size));
  if (space != 0) {
    throw std::system_error(space, std::generic_category(), "posix_fallocate");
  }
  if (config_.operation != Operation::Read ||
      static_cast<std::uint64_t>(info.st_size) >= config_.file_size) {
    return;
  }
  const int flags = fcntl(fd_, F_GETFL);
  if (flags < 0 || fcntl(fd_, F_SETFL, flags & ~O_DIRECT) != 0) {
    throw std::system_error(errno, std::generic_category(), "buffered read setup");
  }
  for (auto offset = static_cast<std::uint64_t>(info.st_size);
       offset < config_.file_size;) {
    const auto count = std::min(config_.block_size, config_.file_size - offset);
    Transfer(fd_, std::span(Buffer(0), count), offset, true);
    offset += count;
  }
  if (fcntl(fd_, F_SETFL, flags) != 0) {
    throw std::system_error(errno, std::generic_category(), "restore I/O flags");
  }
}

}  // namespace iobench
