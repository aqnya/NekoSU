#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace boot {

// One record of a newc ("070701"/"070702") cpio archive. `mode` carries the
// usual st_mode file-type bits, so S_ISDIR(entry.mode) and friends apply.
// Symlink targets are stored in `data`, matching how the kernel reads them.
struct cpio_entry {
  std::string name;
  uint32_t mode = 0;
  uint32_t uid = 0;
  uint32_t gid = 0;
  uint32_t mtime = 0;
  std::vector<uint8_t> data;
};

// Parse a newc cpio archive, stopping at the "TRAILER!!!" record. Returns false
// when the archive is not newc or is truncated.
bool cpio_parse(const uint8_t *data, size_t size, std::vector<cpio_entry> &out);

// Serialize `entries` as a newc cpio archive terminated by the trailer record
// the kernel expects. Records (and the archive) are padded to four bytes.
std::vector<uint8_t> cpio_pack(const std::vector<cpio_entry> &entries);

// Merge `path` into `entries`. When `path` is a directory its tree is merged
// recursively: a host file that matches an existing entry replaces that
// entry's contents in place (keeping the entry's owner and mode), and new
// paths are appended before the trailer with any missing parent directories
// created. When `path` is a single file or symlink it is added at the archive
// root under its base name. Returns false if `path` cannot be read.
bool cpio_merge(std::vector<cpio_entry> &entries, const std::string &path);

} // namespace boot
