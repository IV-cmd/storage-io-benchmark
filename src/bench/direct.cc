#include <fcntl.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <system_error>

#include "bench/io.h"

namespace iobench {

Alignment DirectAlignment(int fd) {
  struct statfs fs {};
  if (fstatfs(fd, &fs) != 0) {
    throw std::system_error(errno, std::generic_category(), "fstatfs");
  }
  if (fs.f_type == TMPFS_MAGIC || fs.f_type == OVERLAYFS_SUPER_MAGIC) {
    throw std::runtime_error(
        "O_DIRECT open succeeded, but tmpfs/OverlayFS is not an accepted "
        "direct-I/O measurement filesystem; no buffered fallback");
  }
  struct statx info {};
  if (statx(fd, "", AT_EMPTY_PATH, STATX_DIOALIGN, &info) != 0) {
    throw std::system_error(errno, std::generic_category(), "statx DIOALIGN");
  }
  if ((info.stx_mask & STATX_DIOALIGN) == 0 || info.stx_dio_mem_align == 0 ||
      info.stx_dio_offset_align == 0) {
    throw std::runtime_error("filesystem did not report direct-I/O alignment");
  }
  const auto device = std::filesystem::canonical(
      "/sys/dev/block/" + std::to_string(info.stx_dev_major) + ":" +
      std::to_string(info.stx_dev_minor));
  std::uint32_t logical = 0;
  for (const auto& path : {device, device.parent_path()}) {
    std::ifstream input(path / "queue/logical_block_size");
    if (input >> logical && logical != 0) {
      break;
    }
  }
  if (logical == 0) {
    throw std::runtime_error("cannot query backing device logical block size");
  }
  return {std::lcm(logical, info.stx_dio_mem_align),
          std::lcm(logical, info.stx_dio_offset_align), logical};
}

void CheckAlignment(const void* buffer, std::uint64_t offset,
                    std::size_t length, Alignment alignment) {
  if (reinterpret_cast<std::uintptr_t>(buffer) % alignment.memory != 0 ||
      offset % alignment.offset != 0 || length % alignment.offset != 0) {
    throw std::invalid_argument(
        "misaligned direct-I/O buffer, offset, or length");
  }
}

}  // namespace iobench
