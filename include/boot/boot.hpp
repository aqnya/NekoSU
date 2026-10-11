#include <cstdio>
#include <string>
namespace boot {
int unpack(const std::string filepath);
int replace(const std::string original, const std::string new_file);
// Merge a file or directory tree into the vendor ramdisk cpio of a v3/v4
// vendor boot image. Kernel modules (*.ko) are routed to lib/modules in the
// ramdisk that already holds that directory; other files keep their relative
// path. Each ramdisk keeps its compression and the v4 ramdisk table is kept
// consistent. Writes `<image_path>.new`.
int patch(const std::string image_path, const std::string patch_path);
}