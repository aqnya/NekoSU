// SPDX-License-Identifier: GPL-3.0-or-later
//
// /data/property/persistent_properties reader/writer.
//
// The file is the protobuf defined by system/core/init/persistent_properties.proto:
//
//   message PersistentProperties { repeated PersistentProperty properties = 1; }
//   message PersistentProperty   { optional string name = 1; optional string value = 2; }
//
// We serialise it by hand so ncore needs no protobuf runtime.
#include "prop/prop_internal.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace prop {
namespace {

constexpr const char *kFile = "/data/property/persistent_properties";

bool read_whole_file(const char *path, std::string &out) {
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  char buf[4096];
  ssize_t n;
  out.clear();
  while ((n = read(fd, buf, sizeof(buf))) > 0) out.append(buf, n);
  close(fd);
  return n >= 0;
}

// ---- protobuf varint ----

bool read_varint(const std::string &b, size_t &pos, uint64_t &out) {
  out = 0;
  int shift = 0;
  while (pos < b.size() && shift < 64) {
    uint8_t c = b[pos++];
    out |= static_cast<uint64_t>(c & 0x7f) << shift;
    if (!(c & 0x80)) return true;
    shift += 7;
  }
  return false;
}

void write_varint(std::string &b, uint64_t v) {
  while (v >= 0x80) {
    b.push_back(static_cast<char>(v | 0x80));
    v >>= 7;
  }
  b.push_back(static_cast<char>(v));
}

// Parse one PersistentProperty sub-message.
bool parse_property(const std::string &b, Property &p) {
  size_t pos = 0;
  while (pos < b.size()) {
    uint64_t tag;
    if (!read_varint(b, pos, tag)) return false;
    uint32_t field = tag >> 3;
    uint32_t wire = tag & 7;
    if (wire != 2) return false;  // only length-delimited here
    uint64_t len;
    if (!read_varint(b, pos, len) || pos + len > b.size()) return false;
    std::string v = b.substr(pos, len);
    pos += len;
    if (field == 1) p.name = std::move(v);
    else if (field == 2) p.value = std::move(v);
  }
  return !p.name.empty();
}

void append_property(std::string &out, const Property &p) {
  std::string sub;
  sub.push_back(0x0a);  // field 1 (name), wire 2
  write_varint(sub, p.name.size());
  sub += p.name;
  sub.push_back(0x12);  // field 2 (value), wire 2
  write_varint(sub, p.value.size());
  sub += p.value;

  out.push_back(0x0a);  // field 1 (properties), wire 2
  write_varint(out, sub.size());
  out += sub;
}

} // namespace

std::vector<Property> LoadPersistentProperties() {
  std::string data;
  if (!read_whole_file(kFile, data)) return {};
  std::vector<Property> props;
  size_t pos = 0;
  while (pos < data.size()) {
    uint64_t tag;
    if (!read_varint(data, pos, tag)) break;
    if ((tag >> 3) != 1 || (tag & 7) != 2) break;
    uint64_t len;
    if (!read_varint(data, pos, len) || pos + len > data.size()) break;
    Property p;
    if (parse_property(data.substr(pos, len), p)) props.push_back(std::move(p));
    pos += len;
  }
  return props;
}

static bool write_all(const std::vector<Property> &props) {
  std::string out;
  for (const auto &p : props) append_property(out, p);

  std::string tmp = std::string(kFile) + ".tmp";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) return false;
  size_t off = 0;
  while (off < out.size()) {
    ssize_t n = write(fd, out.data() + off, out.size() - off);
    if (n <= 0) {
      close(fd);
      unlink(tmp.c_str());
      return false;
    }
    off += static_cast<size_t>(n);
  }
  fsync(fd);
  close(fd);
  if (rename(tmp.c_str(), kFile) != 0) {
    unlink(tmp.c_str());
    return false;
  }
  return true;
}

bool WritePersistentProperty(const std::string &name, const std::string &value) {
  auto props = LoadPersistentProperties();
  bool found = false;
  for (auto &p : props) {
    if (p.name == name) {
      p.value = value;
      found = true;
      break;
    }
  }
  if (!found) props.push_back({name, value});
  return write_all(props);
}

bool DeletePersistentProperty(const std::string &name) {
  auto props = LoadPersistentProperties();
  size_t before = props.size();
  props.erase(std::remove_if(props.begin(), props.end(),
                             [&](const Property &p) { return p.name == name; }),
              props.end());
  if (props.size() == before) return true;
  return write_all(props);
}

} // namespace prop
