// SPDX-License-Identifier: GPL-3.0-or-later
//
// `ncore resetprop`: a Magisk-compatible system-property tool.
//
// get:      resetprop NAME
// set:      resetprop [-n] NAME VALUE
// delete:   resetprop [-p] -d NAME
// list:     resetprop [-Z]
// file:     resetprop [-n] -f FILE      (lines of NAME=VALUE)
// wait:     resetprop -w [--timeout S] NAME [VALUE]
// context:  resetprop -Z NAME
// rebuild:  resetprop [-Z] -c [NAME]
// persistent: -p / -P
#include "prop.hpp"

#include <sys/system_properties.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "prop/prop_internal.hpp"

namespace {

void usage() {
  fprintf(stderr,
          "Usage: resetprop [options] [NAME [VALUE]]\n"
          "  -n, --skip-svc   bypass property_service (direct mmap)\n"
          "  -p, --persistent also touch persistent storage\n"
          "  -P               read only persistent storage\n"
          "  -d, --delete     delete NAME\n"
          "  -w, --wait       wait for NAME [VALUE]\n"
          "      --timeout S  timeout for --wait (seconds)\n"
          "  -f, --file FILE  load NAME=VALUE lines from FILE\n"
          "  -c, --rebuild    rebuild a property area [NAME]\n"
          "      --force      force rebuild\n"
          "  -Z               show SELinux context\n"
          "  -v, --verbose\n");
}

std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Set one property, honouring skip_svc / ro.* (direct) vs property_service.
int set_one(const std::string &name, const std::string &value, bool skip_svc) {
  bool direct = skip_svc || strncmp(name.c_str(), "ro.", 3) == 0;
  if (direct) {
    int ret = prop::SystemProperties::Instance().Set(name, value);
    if (ret != 0) {
      fprintf(stderr, "resetprop: failed to set %s: %d\n", name.c_str(), ret);
      return 1;
    }
  } else {
    if (__system_property_set(name.c_str(), value.c_str()) != 0) {
      fprintf(stderr, "resetprop: failed to set %s\n", name.c_str());
      return 1;
    }
  }
  return 0;
}

int load_file(const std::string &path, bool skip_svc) {
  std::ifstream in(path);
  if (!in) {
    fprintf(stderr, "resetprop: cannot open %s\n", path.c_str());
    return 1;
  }
  std::string line;
  int rc = 0;
  while (std::getline(in, line)) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    size_t eq = t.find('=');
    if (eq == std::string::npos) continue;
    std::string name = trim(t.substr(0, eq));
    std::string value = trim(t.substr(eq + 1));
    if (name.empty()) continue;
    if (set_one(name, value, skip_svc) != 0) rc = 1;
  }
  return rc;
}

} // namespace

int resetprop_main(int argc, char **argv) {
  bool skip_svc = false, persistent = false, persist_only = false, del = false;
  bool verbose = false, wait = false, rebuild = false, show_context = false, force = false;
  bool have_timeout = false;
  double timeout = -1;
  std::string file;
  std::vector<std::string> pos;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-n" || a == "--skip-svc") skip_svc = true;
    else if (a == "-p" || a == "--persistent") persistent = true;
    else if (a == "-P") persist_only = true;
    else if (a == "-d" || a == "--delete") del = true;
    else if (a == "-v" || a == "--verbose") verbose = true;
    else if (a == "-w" || a == "--wait") wait = true;
    else if (a == "-Z") show_context = true;
    else if (a == "-c" || a == "--rebuild" || a == "--compact") rebuild = true;
    else if (a == "--force") force = true;
    else if (a == "--timeout") {
      if (i + 1 >= argc) { usage(); return 1; }
      timeout = atof(argv[++i]);
      have_timeout = true;
    } else if (a == "-f" || a == "--file") {
      if (i + 1 >= argc) { usage(); return 1; }
      file = argv[++i];
    } else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    } else if (a.size() > 1 && a[0] == '-') {
      fprintf(stderr, "resetprop: unknown option %s\n", a.c_str());
      usage();
      return 1;
    } else {
      pos.push_back(a);
    }
  }
  (void)verbose;

  // -P: operate on the persistent file only.
  if (persist_only) {
    if (pos.empty()) {
      for (const auto &p : prop::LoadPersistentProperties())
        printf("[%s]: [%s]\n", p.name.c_str(), p.value.c_str());
      return 0;
    }
    const std::string &name = pos[0];
    for (const auto &p : prop::LoadPersistentProperties()) {
      if (p.name == name) {
        printf("%s\n", p.value.c_str());
        return 0;
      }
    }
    fprintf(stderr, "resetprop: %s not found\n", name.c_str());
    return 1;
  }

  auto &sys = prop::SystemProperties::Instance();
  bool have_areas = sys.Init();

  if (!file.empty()) return load_file(file, skip_svc);

  if (wait) {
    if (pos.empty()) { fprintf(stderr, "resetprop: -w requires NAME\n"); return 1; }
    std::optional<std::string> old;
    if (pos.size() >= 2) old = pos[1];
    if (!sys.Wait(pos[0], old, have_timeout ? timeout : -1)) return 2;
    return 0;
  }

  if (rebuild) {
    if (!have_areas) { fprintf(stderr, "resetprop: property areas unavailable\n"); return 1; }
    // Without NAME, only rebuild when explicitly forced (mirrors Magisk).
    if (pos.empty() && !force) {
      fprintf(stderr, "resetprop: -c without NAME needs --force\n");
      return 1;
    }
    if (!sys.Rebuild(pos.empty() ? "" : pos[0], force)) {
      fprintf(stderr, "resetprop: rebuild failed\n");
      return 1;
    }
    return 0;
  }

  if (del) {
    if (pos.empty()) { fprintf(stderr, "resetprop: -d requires NAME\n"); return 1; }
    const std::string &name = pos[0];
    bool ok = have_areas && sys.Delete(name);
    if (persistent || strncmp(name.c_str(), "persist.", 8) == 0) prop::DeletePersistentProperty(name);
    if (!ok) { fprintf(stderr, "resetprop: %s not found\n", name.c_str()); return 1; }
    return 0;
  }

  // get
  if (pos.size() == 1) {
    const std::string &name = pos[0];
    if (show_context) {
      if (!have_areas) return 1;
      printf("%s\n", sys.GetContext(name).c_str());
      return 0;
    }
    auto v = have_areas ? sys.Get(name) : std::nullopt;
    if (!v.has_value()) {
      char buf[PROP_VALUE_MAX] = {};
      if (__system_property_get(name.c_str(), buf) > 0) v = std::string(buf);
    }
    if (!v.has_value()) { fprintf(stderr, "resetprop: %s not found\n", name.c_str()); return 1; }
    printf("%s\n", v->c_str());
    return 0;
  }

  // set
  if (pos.size() >= 2) {
    const std::string &name = pos[0];
    const std::string &value = pos[1];
    if (skip_svc && !have_areas) {
      fprintf(stderr, "resetprop: direct set unavailable\n");
      return 1;
    }
    if (set_one(name, value, skip_svc) != 0) return 1;
    if (persistent || strncmp(name.c_str(), "persist.", 8) == 0 ||
        strncmp(name.c_str(), "next_boot.", 10) == 0)
      prop::WritePersistentProperty(name, value);
    return 0;
  }

  // list
  if (!have_areas) { fprintf(stderr, "resetprop: property areas unavailable\n"); return 1; }
  sys.ForEach([&](const prop::Property &p) {
    if (show_context)
      printf("[%s]: [%s] [%s]\n", p.name.c_str(), p.value.c_str(),
             sys.GetContext(p.name).c_str());
    else
      printf("[%s]: [%s]\n", p.name.c_str(), p.value.c_str());
  });
  return 0;
}
