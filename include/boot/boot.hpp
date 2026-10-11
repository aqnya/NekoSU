#include <cstdio>
#include <string>
namespace boot {
int unpack(const std::string filepath);
int replace(const std::string original, const std::string new_file);
// Patch a v3/v4 vendor boot image by merging `patch_path` into its vendor
// ramdisk cpio(s). `patch_path` is a directory or single file merged at its
// relative path (every *.ko is installed at lib/modules/<name>, replacing an
// existing entry of the same name), or `dest=source` to place `source` at a
// specific archive path, replacing whatever is there. Each ramdisk keeps its
// compression and the v4 ramdisk table is kept consistent. Writes
// `<image_path>.new`.
int patch(const std::string image_path, const std::string patch_path);
}