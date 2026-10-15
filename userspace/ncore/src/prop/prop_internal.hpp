// SPDX-License-Identifier: GPL-3.0-or-later
//
// Android system-property access for ncore's `resetprop`.
//
// The model mirrors AOSP: bionic's on-disk `prop_area` / `prop_info` layout and
// its single-writer serial protocol (libc/system_properties), the
// `property_info` context trie (system/core/property_service/libpropertyinfoparser)
// and the persistent-property file (system/core/init/persistent_properties).
//
// Unlike bionic, which mmaps the property areas read-only for non-init
// processes, we map them read-write so a root process can add/update/delete
// properties directly (the `-n` "skip property_service" behaviour).  The
// changes land in the shared mapping, so every other process observes them.
#ifndef NKSU_PROP_INTERNAL_HPP
#define NKSU_PROP_INTERNAL_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace prop {

// Snapshot of one property, used by listing and by `-f` round-trips.
struct Property {
  std::string name;
  std::string value;
};

// System-property manager.  Singleton; call Init() once before use.
class SystemProperties {
 public:
  static SystemProperties &Instance();

  // Detect the property layout (/dev/__properties__), load the context trie and
  // map the serial area read-write.  Returns false when the areas cannot be
  // reached (e.g. not root), in which case only __system_property_* reads work.
  bool Init();

  // Read a property.  Returns std::nullopt when it does not exist.
  std::optional<std::string> Get(const std::string &name);

  // SELinux context string for a property name ("u:object_r:...:s0").
  std::string GetContext(const std::string &name);

  // Direct add/update (bypasses property_service; works for ro.* too).
  // Returns 0 on success, negative errno-style on failure.
  int Set(const std::string &name, const std::string &value);

  // Remove a property from the shared area. Returns true when it existed.
  bool Delete(const std::string &name);

  // Visit every property across every mapped area.
  void ForEach(const std::function<void(const Property &)> &fn);

  // Wait until `name` exists (old_value == nullopt) or its value differs from
  // `old_value`.  timeout_sec < 0 waits forever.  Returns true when satisfied.
  bool Wait(const std::string &name, const std::optional<std::string> &old_value,
            double timeout_sec);

  // All known SELinux context names (for `-c`).
  std::vector<std::string> Contexts();

  // Compact a property area (the one holding `name`, or every area when empty)
  // by rebuilding it from its live properties.
  bool Rebuild(const std::string &name, bool force);

 private:
  SystemProperties();
  ~SystemProperties();

  // Internal per-directory state (defined in prop.cc); `appcompat_` is null
  // when the Android 14+ override directory is absent.
  struct PropContext;
  PropContext *main_;
  PropContext *appcompat_;
};

// Persistent-property file (/data/property/persistent_properties), protobuf.
std::vector<Property> LoadPersistentProperties();
bool WritePersistentProperty(const std::string &name, const std::string &value);
bool DeletePersistentProperty(const std::string &name);

} // namespace prop

#endif // NKSU_PROP_INTERNAL_HPP
