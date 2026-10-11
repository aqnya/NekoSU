#include "boot_internal.hpp"
#include "bootimg.h"
#include "cpio.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace boot {
namespace {

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

// Unpack one compressed cpio blob, merge the patch path into it and repack it
// using the blob's original encoding.
bool patch_blob(const uint8_t *data, size_t size, const std::string &patch,
                const std::string &tmp, std::vector<uint8_t> &out) {
  const char *format = detect_format(data, size);

  std::vector<uint8_t> raw;
  if (format != nullptr) {
    if (!decompress_to_vector(data, size, format, tmp, raw)) {
      std::cerr << "patch: failed to decompress vendor ramdisk\n";
      return false;
    }
  } else {
    raw.assign(data, data + size);
  }

  std::vector<cpio_entry> entries;
  if (!cpio_parse(raw.data(), raw.size(), entries)) {
    std::cerr << "patch: vendor ramdisk is not a newc cpio archive\n";
    return false;
  }

  if (!cpio_merge(entries, patch)) {
    std::cerr << "patch: cannot read patch path " << patch << std::endl;
    return false;
  }

  const std::vector<uint8_t> packed = cpio_pack(entries);
  return compress_to_vector(format, packed.data(), packed.size(), tmp, out);
}

} // namespace

// Patch `image_path` -- a v3/v4 vendor boot image -- in place by merging
// `patch_path` into its vendor ramdisk cpio(s). The ramdisk keeps its original
// compression and, for v4, the ramdisk table is updated to match. The result is
// written to `<image_path>.new`.
int patch(const std::string image_path, const std::string patch_path) {
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

    // Rebuild the concatenated ramdisk section one table entry at a time so
    // every sub-ramdisk keeps its own compression and the table stays in sync.
    new_table.assign(vtable->data, vtable->data + vtable->size);
    for (uint32_t i = 0; i < head.vendor_ramdisk_table_entry_num; ++i) {
      const size_t entry_off =
          static_cast<size_t>(i) * head.vendor_ramdisk_table_entry_size;
      vendor_ramdisk_table_entry_v4 entry{};
      std::memcpy(&entry, new_table.data() + entry_off, sizeof(entry));

      if (entry.ramdisk_size == 0)
        continue;

      if (entry.ramdisk_offset > vramdisk->size ||
          entry.ramdisk_size > vramdisk->size - entry.ramdisk_offset) {
        std::cerr << "patch: vendor ramdisk table entry " << i
                  << " out of bounds\n";
        return -1;
      }

      std::vector<uint8_t> patched;
      const std::string entry_tmp = tmp + "." + std::to_string(i);
      if (!patch_blob(vramdisk->data + entry.ramdisk_offset, entry.ramdisk_size,
                      patch_path, entry_tmp, patched)) {
        return -1;
      }

      entry.ramdisk_offset = static_cast<uint32_t>(new_ramdisk.size());
      entry.ramdisk_size = static_cast<uint32_t>(patched.size());
      new_ramdisk.insert(new_ramdisk.end(), patched.begin(), patched.end());
      std::memcpy(new_table.data() + entry_off, &entry, sizeof(entry));
    }
  } else {
    if (!patch_blob(vramdisk->data, vramdisk->size, patch_path, tmp,
                    new_ramdisk)) {
      return -1;
    }
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
  if (!rebuild_image(image, sections, page, header_size,
                     std::string::npos, out_path)) {
    std::cerr << "patch: failed to write " << out_path << std::endl;
    return -1;
  }

  std::cout << "patched vendor ramdisk -> " << out_path << std::endl;
  return 0;
}

} // namespace boot
