#include "cpio.hpp"

#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

namespace boot {
namespace {

// newc ("SVR4 without CRC") record layout: a 110-byte ASCII header followed by
// a NUL-terminated name and the file data, both padded to four bytes.
constexpr size_t kHeaderSize = 110;
constexpr size_t kFieldSize = 8;
constexpr const char *kMagic = "070701";
constexpr const char *kMagicCrc = "070702";
constexpr const char *kTrailer = "TRAILER!!!";

// Index of each field in the header, after the six magic bytes.
constexpr size_t kFieldMode = 1;
constexpr size_t kFieldUid = 2;
constexpr size_t kFieldGid = 3;
constexpr size_t kFieldMtime = 5;
constexpr size_t kFieldFilesize = 6;
constexpr size_t kFieldNamesize = 11;

size_t align4(size_t value) { return (value + 3) & ~static_cast<size_t>(3); }

bool hex_digit(char c, uint32_t &out) {
  if (c >= '0' && c <= '9')
    out = static_cast<uint32_t>(c - '0');
  else if (c >= 'a' && c <= 'f')
    out = static_cast<uint32_t>(c - 'a' + 10);
  else if (c >= 'A' && c <= 'F')
    out = static_cast<uint32_t>(c - 'A' + 10);
  else
    return false;
  return true;
}

bool read_field(const uint8_t *header, size_t index, uint32_t &value) {
  const char *field = reinterpret_cast<const char *>(header) + 6 + index * kFieldSize;
  value = 0;
  for (size_t i = 0; i < kFieldSize; ++i) {
    uint32_t digit;
    if (!hex_digit(field[i], digit))
      return false;
    value = (value << 4) | digit;
  }
  return true;
}

std::string hex_field(uint32_t value) {
  static const char digits[] = "0123456789ABCDEF";
  std::string out(kFieldSize, '0');
  for (size_t i = kFieldSize; i-- > 0;) {
    out[i] = digits[value & 0xf];
    value >>= 4;
  }
  return out;
}

void append_record(std::vector<uint8_t> &out, const cpio_entry &entry,
                   uint32_t ino) {
  const uint32_t namesize = static_cast<uint32_t>(entry.name.size() + 1);
  const uint32_t filesize = static_cast<uint32_t>(entry.data.size());

  std::string header(kMagic);
  header += hex_field(ino);
  header += hex_field(entry.mode);
  header += hex_field(entry.uid);
  header += hex_field(entry.gid);
  header += hex_field(1); // nlink
  header += hex_field(entry.mtime);
  header += hex_field(filesize);
  header += hex_field(0); // devmajor
  header += hex_field(0); // devminor
  header += hex_field(0); // rdevmajor
  header += hex_field(0); // rdevminor
  header += hex_field(namesize);
  header += hex_field(0); // check

  out.insert(out.end(), header.begin(), header.end());
  out.insert(out.end(), entry.name.begin(), entry.name.end());
  out.push_back(0);
  out.resize(align4(out.size()), 0);
  out.insert(out.end(), entry.data.begin(), entry.data.end());
  out.resize(align4(out.size()), 0);
}

// Ramdisk archives name entries relative to the root, but some tools prefix
// them with "./". Strip it so either spelling compares equal.
std::string normalize(const std::string &name) {
  std::string out = name;
  while (out.rfind("./", 0) == 0)
    out.erase(0, 2);
  return out;
}

bool read_file(const std::string &path, std::vector<uint8_t> &out) {
  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return false;

  out.clear();
  uint8_t buffer[64 << 10];
  ssize_t n;
  while ((n = ::read(fd, buffer, sizeof(buffer))) > 0)
    out.insert(out.end(), buffer, buffer + n);

  ::close(fd);
  return n == 0;
}

void replace_or_add(std::vector<cpio_entry> &entries,
                    std::map<std::string, size_t> &index,
                    const std::string &name, uint32_t add_mode,
                    std::vector<uint8_t> data) {
  const auto it = index.find(name);
  if (it != index.end()) {
    // Existing entry: swap only the payload so the original owner, mode and
    // timestamp survive. Update every entry that resolves to the same name so
    // archives carrying both "x" and "./x" stay consistent.
    const std::string key = normalize(name);
    for (cpio_entry &entry : entries)
      if (normalize(entry.name) == key)
        entry.data = data;
    return;
  }

  cpio_entry entry;
  entry.name = name;
  entry.mode = add_mode;
  entry.data = std::move(data);
  entries.push_back(std::move(entry));
  index[name] = entries.size() - 1;
}

// Create a 0755 directory entry for every missing parent of `name`.
void ensure_parents(std::vector<cpio_entry> &entries,
                    std::map<std::string, size_t> &index,
                    const std::string &name) {
  for (size_t pos = name.find('/'); pos != std::string::npos;
       pos = name.find('/', pos + 1)) {
    const std::string dir = name.substr(0, pos);
    if (!dir.empty() && index.find(dir) == index.end())
      replace_or_add(entries, index, dir, S_IFDIR | 0755, {});
  }
}

} // namespace

bool cpio_parse(const uint8_t *data, size_t size, std::vector<cpio_entry> &out) {
  out.clear();

  size_t pos = 0;
  while (pos + kHeaderSize <= size) {
    const uint8_t *header = data + pos;
    const char *magic = reinterpret_cast<const char *>(header);

    if (std::memcmp(magic, kMagic, 6) != 0 &&
        std::memcmp(magic, kMagicCrc, 6) != 0) {
      // Garbage before the first record means this is not a cpio archive;
      // garbage after at least one record is trailing padding we can ignore.
      return !out.empty();
    }

    uint32_t mode = 0, uid = 0, gid = 0, mtime = 0, filesize = 0, namesize = 0;
    if (!read_field(header, kFieldMode, mode) ||
        !read_field(header, kFieldUid, uid) ||
        !read_field(header, kFieldGid, gid) ||
        !read_field(header, kFieldMtime, mtime) ||
        !read_field(header, kFieldFilesize, filesize) ||
        !read_field(header, kFieldNamesize, namesize) || namesize == 0) {
      return false;
    }

    const size_t name_off = pos + kHeaderSize;
    if (namesize > size - name_off)
      return false;

    std::string name(reinterpret_cast<const char *>(data + name_off), namesize);
    if (!name.empty() && name.back() == '\0')
      name.pop_back();

    const size_t data_off = align4(name_off + namesize);
    if (data_off > size || filesize > size - data_off)
      return false;

    if (name == kTrailer)
      return true;

    cpio_entry entry;
    entry.name = std::move(name);
    entry.mode = mode;
    entry.uid = uid;
    entry.gid = gid;
    entry.mtime = mtime;
    entry.data.assign(data + data_off, data + data_off + filesize);
    out.push_back(std::move(entry));

    pos = align4(data_off + filesize);
  }

  return !out.empty();
}

std::vector<uint8_t> cpio_pack(const std::vector<cpio_entry> &entries) {
  std::vector<uint8_t> out;
  uint32_t ino = 1;

  for (const cpio_entry &entry : entries)
    append_record(out, entry, ino++);

  cpio_entry trailer;
  trailer.name = kTrailer;
  append_record(out, trailer, ino);

  return out;
}

bool cpio_put(std::vector<cpio_entry> &entries, const std::string &name,
              const std::string &host_path) {
  struct stat st{};
  if (::lstat(host_path.c_str(), &st) != 0)
    return false;

  std::map<std::string, size_t> index;
  for (size_t i = 0; i < entries.size(); ++i)
    index[normalize(entries[i].name)] = i;

  ensure_parents(entries, index, name);

  if (S_ISLNK(st.st_mode)) {
    char target[PATH_MAX];
    const ssize_t len = ::readlink(host_path.c_str(), target, sizeof(target));
    if (len < 0)
      return false;
    std::vector<uint8_t> data(target, target + len);
    replace_or_add(entries, index, name, S_IFLNK | 0777, std::move(data));
    return true;
  }

  if (S_ISREG(st.st_mode)) {
    std::vector<uint8_t> data;
    if (!read_file(host_path, data))
      return false;
    replace_or_add(entries, index, name, S_IFREG | (st.st_mode & 07777),
                   std::move(data));
    return true;
  }

  return false;
}

} // namespace boot
