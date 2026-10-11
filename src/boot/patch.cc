#include "boot_internal.hpp"
#include "bootimg.h"
#include "cpio.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace boot {
namespace {

// The ncore kernel module always lives at this path inside the vendor ramdisk.
constexpr const char *kNksuModule = "lib/modules/nksu.ko";

template <typename T>
bool read_struct(const MMapFile &image, size_t offset, T &out) {
  if (offset > image.size() || sizeof(T) > image.size() - offset)
    return false;

  std::memcpy(&out, image.data() + offset, sizeof(T));
  return true;
}

bool read_file(const char *path, std::vector<uint8_t> &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;

  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  in.seekg(0, std::ios::beg);
  if (size < 0)
    return false;

  out.resize(static_cast<size_t>(size));
  if (size > 0)
    in.read(reinterpret_cast<char *>(out.data()), size);

  return in.good() || in.eof();
}

std::string normalize(const std::string &name) {
  std::string out = name;
  while (out.rfind("./", 0) == 0)
    out.erase(0, 2);
  return out;
}

// Decompress `data`, whose detected format is `format`, through a temporary
// file (the decompressors all write to a path) and read the result back.
bool decompress_to_vector(const uint8_t *data, size_t size, const char *format,
                          const std::string &tmp, std::vector<uint8_t> &out) {
  const std::string name(format);
  bool ok;
  if (name == "gzip")
    ok = decompress_gzip(tmp.c_str(), data, size);
  else if (name == "bzip2")
    ok = decompress_bzip2(tmp.c_str(), data, size);
  else if (name == "lzo")
    ok = decompress_lzop(tmp.c_str(), data, size);
  else if (name == "xz")
    ok = decompress_xz(tmp.c_str(), data, size);
  else if (name == "lzma")
    ok = decompress_lzma(tmp.c_str(), data, size);
  else if (name == "lz4_lg")
    ok = decompress_lz4_lg(tmp.c_str(), data, size);
  else if (name == "lz4_leg")
    ok = decompress_lz4_legacy(tmp.c_str(), data, size);
  else if (name == "lz42")
    ok = decompress_lz4(tmp.c_str(), data, size);
  else
    return false;

  if (!ok) {
    std::remove(tmp.c_str());
    return false;
  }

  const bool read = read_file(tmp.c_str(), out);
  std::remove(tmp.c_str());
  return read;
}

// Compress `data` into `format` through a temporary file and read it back. A
// null format copies the data through uncompressed.
bool compress_to_vector(const char *format, const uint8_t *data, size_t size,
                        const std::string &tmp, std::vector<uint8_t> &out) {
  if (!compress_section(tmp.c_str(), data, size, format)) {
    std::remove(tmp.c_str());
    return false;
  }

  const bool read = read_file(tmp.c_str(), out);
  std::remove(tmp.c_str());
  return read;
}

// A loaded vendor ramdisk sub-image: its original encoding and the decompressed
// cpio records.
struct ramdisk {
  size_t size = 0;
  const char *format = nullptr;
  std::vector<cpio_entry> entries;
};

bool load_ramdisk(const uint8_t *data, size_t size, const std::string &tmp,
                  ramdisk &out) {
  out.size = size;
  out.format = detect_format(data, size);

  std::vector<uint8_t> raw;
  if (out.format != nullptr) {
    if (!decompress_to_vector(data, size, out.format, tmp, raw)) {
      std::cerr << "patch: failed to decompress vendor ramdisk\n";
      return false;
    }
  } else {
    raw.assign(data, data + size);
  }

  if (!cpio_parse(raw.data(), raw.size(), out.entries)) {
    std::cerr << "patch: vendor ramdisk is not a newc cpio archive\n";
    return false;
  }

  return true;
}

bool store_ramdisk(const ramdisk &rd, const std::string &tmp,
                   std::vector<uint8_t> &out) {
  const std::vector<uint8_t> packed = cpio_pack(rd.entries);
  return compress_to_vector(rd.format, packed.data(), packed.size(), tmp, out);
}

// Whether the archive already carries lib/modules, marking it as the ramdisk
// the module belongs in.
bool has_modules(const std::vector<cpio_entry> &entries) {
  const std::string target = normalize(kNksuModule);
  const std::string dir = target.substr(0, target.find_last_of('/'));
  for (const cpio_entry &entry : entries) {
    const std::string name = normalize(entry.name);
    if (name == dir || name.rfind(dir + "/", 0) == 0)
      return true;
  }
  return false;
}

} // namespace

// Install `ko_path` as lib/modules/nksu.ko in the vendor ramdisk of
// `image_path` (a v3/v4 vendor boot image), replacing the module that is
// already there. The ramdisk keeps its original compression and, for v4, the
// ramdisk table is updated to match. The result is written to
// `<image_path>.new`.
int patch(const std::string image_path, const std::string ko_path) {
  struct stat ko_st{};
  if (::lstat(ko_path.c_str(), &ko_st) != 0 || !S_ISREG(ko_st.st_mode)) {
    std::cerr << "patch: " << ko_path << " is not a regular file\n";
    return -1;
  }

  MMapFile image(image_path.c_str());
  if (!image.valid()) {
    std::cerr << "patch: cannot open " << image_path << std::endl;
    return -1;
  }

  const boottype b = probe_type(image);
  if (b != boottype::vendor_boot_img_hdr_v3 &&
      b != boottype::vendor_boot_img_hdr_v4) {
    std::cerr << "patch: only vendor boot images can be patched directly\n";
    return -1;
  }

  std::vector<section> sections;
  size_t page = 0;
  size_t header_size = 0;
  if (!collect_sections(image, b, page, header_size, sections))
    return -1;

  section *vramdisk = nullptr;
  section *vtable = nullptr;
  for (section &s : sections) {
    if (s.name == "vendor_ramdisk")
      vramdisk = &s;
    else if (s.name == "vendor_ramdisk_table")
      vtable = &s;
  }

  if (vramdisk == nullptr || vramdisk->size == 0) {
    std::cerr << "patch: image has no vendor ramdisk to patch\n";
    return -1;
  }

  const std::string tmp = image_path + ".patch";
  std::vector<uint8_t> new_ramdisk;
  std::vector<uint8_t> new_table;

  if (b == boottype::vendor_boot_img_hdr_v4) {
    vendor_boot_img_hdr_v4 head{};
    if (!read_struct(image, 0, head) ||
        head.vendor_ramdisk_table_entry_size !=
            sizeof(vendor_ramdisk_table_entry_v4) ||
        head.vendor_ramdisk_table_entry_num == 0 || vtable == nullptr ||
        vtable->size <
            static_cast<size_t>(head.vendor_ramdisk_table_entry_num) *
                head.vendor_ramdisk_table_entry_size) {
      std::cerr << "patch: unusable vendor ramdisk table\n";
      return -1;
    }

    const uint32_t count = head.vendor_ramdisk_table_entry_num;
    const size_t entry_size = head.vendor_ramdisk_table_entry_size;

    // Parse every sub-ramdisk up front so the module-bearing one can be picked
    // before anything is written.
    std::vector<vendor_ramdisk_table_entry_v4> table(count);
    std::vector<ramdisk> rds(count);
    int module_target = -1;
    for (uint32_t i = 0; i < count; ++i) {
      std::memcpy(&table[i], vtable->data + static_cast<size_t>(i) * entry_size,
                  sizeof(table[i]));

      if (table[i].ramdisk_size == 0)
        continue;

      if (table[i].ramdisk_offset > vramdisk->size ||
          table[i].ramdisk_size > vramdisk->size - table[i].ramdisk_offset) {
        std::cerr << "patch: vendor ramdisk table entry " << i
                  << " out of bounds\n";
        return -1;
      }

      const std::string entry_tmp = tmp + "." + std::to_string(i);
      if (!load_ramdisk(vramdisk->data + table[i].ramdisk_offset,
                        table[i].ramdisk_size, entry_tmp, rds[i])) {
        return -1;
      }

      if (module_target < 0 && has_modules(rds[i].entries))
        module_target = static_cast<int>(i);
    }

    // No ramdisk carries lib/modules yet: install into the first one so the
    // module still lands somewhere predictable.
    if (module_target < 0) {
      for (uint32_t i = 0; i < count; ++i) {
        if (rds[i].size != 0) {
          module_target = static_cast<int>(i);
          break;
        }
      }
    }

    // Rebuild the concatenated ramdisk section one entry at a time so every
    // sub-ramdisk keeps its own compression and the table stays in sync.
    for (uint32_t i = 0; i < count; ++i) {
      vendor_ramdisk_table_entry_v4 &entry = table[i];
      if (entry.ramdisk_size == 0)
        continue;

      if (static_cast<int>(i) == module_target &&
          !cpio_put(rds[i].entries, kNksuModule, ko_path)) {
        std::cerr << "patch: failed to install " << ko_path << " into ramdisk "
                  << i << std::endl;
        return -1;
      }

      std::vector<uint8_t> patched;
      const std::string entry_tmp = tmp + "." + std::to_string(i);
      if (!store_ramdisk(rds[i], entry_tmp, patched))
        return -1;

      entry.ramdisk_offset = static_cast<uint32_t>(new_ramdisk.size());
      entry.ramdisk_size = static_cast<uint32_t>(patched.size());
      new_ramdisk.insert(new_ramdisk.end(), patched.begin(), patched.end());
    }

    new_table.resize(static_cast<size_t>(count) * entry_size);
    for (uint32_t i = 0; i < count; ++i)
      std::memcpy(new_table.data() + static_cast<size_t>(i) * entry_size,
                  &table[i], sizeof(table[i]));
  } else {
    ramdisk rd;
    if (!load_ramdisk(vramdisk->data, vramdisk->size, tmp, rd))
      return -1;

    if (!cpio_put(rd.entries, kNksuModule, ko_path)) {
      std::cerr << "patch: failed to install " << ko_path << std::endl;
      return -1;
    }

    if (!store_ramdisk(rd, tmp, new_ramdisk))
      return -1;
  }

  vramdisk->data = new_ramdisk.data();
  vramdisk->size = new_ramdisk.size();

  // The table keeps its entry count, so its own size and the header's entry
  // count are unchanged; rebuild_image rewrites the section size fields.
  if (!new_table.empty()) {
    vtable->data = new_table.data();
    vtable->size = new_table.size();
  }

  const std::string out_path = image_path + ".new";
  if (!rebuild_image(image, sections, page, header_size, std::string::npos,
                     out_path)) {
    std::cerr << "patch: failed to write " << out_path << std::endl;
    return -1;
  }

  std::cout << "installed " << ko_path << " -> " << kNksuModule << " in "
            << out_path << std::endl;
  return 0;
}

} // namespace boot
