#include <cstdio>
#include <string>
namespace boot {
int unpack(const std::string filepath);
int replace(const std::string original, const std::string new_file);
// Install `ko_path` as lib/modules/nksu.ko in the vendor ramdisk of a v3/v4
// vendor boot image, replacing the module already there. Each ramdisk keeps its
// compression and the v4 ramdisk table stays consistent. Writes
// `<image_path>.new`.
int patch(const std::string image_path, const std::string ko_path);
}