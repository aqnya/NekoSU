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

// Add or replace a single entry named `name` inside the archive, reading from
// the host file `host_path` (a regular file or symlink). Missing parent
// directories are created. The entry named exactly `name` (including any "./"
// prefix) is updated in place, keeping its owner and mode; only if no such
// entry exists is an existing one that differs solely by the "./" prefix
// reused, so no duplicate is introduced. Returns false if the host file cannot
// be read.
bool cpio_put(std::vector<cpio_entry> &entries, const std::string &name,
              const std::string &host_path);

} // namespace boot
