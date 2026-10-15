#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "su/su.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <iostream>
#include <linux/capability.h>
#include <pwd.h>
#include <sched.h>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace su {

namespace {

constexpr const char *kDefaultShell = "/system/bin/sh";
// Root-shell PATH entries: the KernelSU-compatible bin dir (which holds the
// resetprop symlink and busybox) and its nksu alias.
constexpr const char *kNksuBinDir = "/data/adb/nksu/bin";
constexpr const char *kKsuBinDir = "/data/adb/ksu/bin";
constexpr const char *kNksuDir = "/data/adb/nksu";

std::string base_name(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

void print_usage() {
  std::cout << "Usage: su [options] [-] [user [argument...]]\n"
               "\n"
               "Options:\n"
               "  -c, --command COMMAND        pass COMMAND to the shell\n"
               "  -s, --shell SHELL            use SHELL instead of /system/bin/sh\n"
               "  -u, --user USER              run as USER (a name or a uid)\n"
               "  -g, --group GROUP            run with GROUP as the primary group\n"
               "  -G, --supp-group GROUP       replace the supplementary groups with GROUP\n"
               "  -p, --preserve-environment   keep the current environment\n"
               "  -l, --login                  start a login shell\n"
               "  -M, --mount-master           run in the global mount namespace\n"
               "  -Z, --context CONTEXT        set the SELinux context\n"
               "  -v, --version                show version\n"
               "  -h, --help                   show this help\n"
            << std::endl;
}

// Prepend a directory to PATH if it is not already a path component.
void prepend_path_dir(const std::string &dir) {
  const char *path_env = ::getenv("PATH");
  const std::string path =
      path_env != nullptr ? path_env : "/sbin:/system/sbin:/system/bin:/system/xbin";
  size_t start = 0;
  while (start <= path.size()) {
    const size_t end = path.find(':', start);
    const std::string comp =
        path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (comp == dir)
      return;
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  ::setenv("PATH", (dir + ":" + path).c_str(), 1);
}

void switch_to_global_mount_ns() {
  const int fd = ::open("/proc/1/ns/mnt", O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return;
  ::setns(fd, CLONE_NEWNS);
  ::close(fd);
}

void set_selinux_context(const std::string &context) {
  const int fd = ::open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return;
  const ssize_t written = ::write(fd, context.c_str(), context.size());
  (void)written;
  ::close(fd);
}

// ---- target identity ------------------------------------------------------

struct Identity {
  uid_t uid = 0;
  gid_t gid = 0;
  std::string name;
  std::string home;
};

// Parse a decimal uid/gid; rejects anything that is not a plain number.
bool parse_id(const std::string &spec, unsigned long &value) {
  if (spec.empty())
    return false;
  for (const char c : spec) {
    if (!std::isdigit(static_cast<unsigned char>(c)))
      return false;
  }
  errno = 0;
  char *end = nullptr;
  value = std::strtoul(spec.c_str(), &end, 10);
  return errno == 0 && end != nullptr && *end == '\0' &&
         value <= 0xffffffffUL;
}

void set_name_home(Identity &id, const struct passwd *pw, const std::string &fallback_name) {
  if (pw != nullptr && pw->pw_name != nullptr && pw->pw_name[0] != '\0')
    id.name = pw->pw_name;
  else
    id.name = fallback_name;
  if (pw != nullptr && pw->pw_dir != nullptr && pw->pw_dir[0] != '\0')
    id.home = pw->pw_dir;
  else
    id.home = "/";
}

// Resolve a user name or a numeric uid.  An unknown numeric uid is kept as
// is with gid == uid, matching how Android app users (u0_aNNN) are laid out.
bool resolve_user(const std::string &spec, Identity &id, std::string &error) {
  unsigned long value = 0;
  if (parse_id(spec, value)) {
    id.uid = static_cast<uid_t>(value);
    id.gid = static_cast<gid_t>(value);
    const struct passwd *pw = ::getpwuid(id.uid);
    if (pw != nullptr)
      id.gid = pw->pw_gid;
    set_name_home(id, pw, spec);
    return true;
  }

  const struct passwd *pw = ::getpwnam(spec.c_str());
  if (pw == nullptr) {
    error = "unknown user: " + spec;
    return false;
  }
  id.uid = pw->pw_uid;
  id.gid = pw->pw_gid;
  set_name_home(id, pw, spec);
  return true;
}

bool resolve_group(const std::string &spec, gid_t &gid, std::string &error) {
  unsigned long value = 0;
  if (parse_id(spec, value)) {
    gid = static_cast<gid_t>(value);
    return true;
  }
  const struct group *gr = ::getgrnam(spec.c_str());
  if (gr == nullptr) {
    error = "unknown group: " + spec;
    return false;
  }
  gid = gr->gr_gid;
  return true;
}

// True when we may change the process credentials.  The kernel escalates the
// redirected `su` before we run, but this binary can also be started through
// the `ncore su` subcommand from an unprivileged, seccomp-filtered process
// (where setgroups(2) and friends are killed with SIGSYS instead of failing
// with EPERM), so look before stepping.
bool can_switch_user() {
  if (::geteuid() == 0)
    return true;

  struct __user_cap_header_struct hdr = {_LINUX_CAPABILITY_VERSION_3, 0};
  struct __user_cap_data_struct data[2] = {{0, 0, 0}, {0, 0, 0}};
  if (::syscall(__NR_capget, &hdr, data) != 0)
    return false;

  // CAP_SETGID also governs setgroups(2).
  const unsigned int needed = (1u << CAP_SETUID) | (1u << CAP_SETGID);
  return (data[0].effective & needed) == needed;
}

// Drop from the identity the kernel gave us to `target`.  Supplementary
// groups come from `supp_groups` when `explicit_groups` is set (the -G
// option), otherwise from the target user's group database entry.
bool drop_privileges(const Identity &target, const std::vector<gid_t> &supp_groups,
                     bool explicit_groups, std::string &error) {
  if (target.uid == ::geteuid() && target.gid == ::getegid() && !explicit_groups)
    return true; // already there

  if (!can_switch_user()) {
    error = "cannot switch to user " + target.name + ": Operation not permitted";
    return false;
  }

  // Supplementary groups first: everything below drops the privilege that
  // changes them.
  if (explicit_groups) {
    if (::setgroups(static_cast<int>(supp_groups.size()), supp_groups.data()) != 0) {
      error = "cannot set groups: " + std::string(std::strerror(errno));
      return false;
    }
  } else if (::initgroups(target.name.c_str(), target.gid) != 0) {
    // No usable group database entry for this user: fall back to the primary
    // group alone (what setgroups(1, &gid) below would have done).
    const gid_t gid = target.gid;
    if (::setgroups(1, &gid) != 0) {
      error = "cannot set groups: " + std::string(std::strerror(errno));
      return false;
    }
  }

  if (::setgid(target.gid) != 0) {
    error = "cannot change group: " + std::string(std::strerror(errno));
    return false;
  }
  if (::setuid(target.uid) != 0) {
    error = "cannot switch to user " + target.name + ": " +
            std::string(std::strerror(errno));
    return false;
  }
  return true;
}

} // namespace

bool is_su_invocation(const char *arg0) {
  return arg0 != nullptr && base_name(arg0) == "su";
}

int run(int argc, char *argv[]) {
  std::string shell;
  std::string command;
  std::string context;
  std::string user_spec;
  std::string group_spec;
  std::vector<std::string> supp_specs;
  bool have_command = false;
  bool preserve_env = false;
  bool login = false;
  bool mount_master = false;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] != nullptr ? argv[i] : "";

    if (arg == "-c" || arg == "--command") {
      // Android su (and KernelSU's ksud) treat everything after -c as the
      // command, joining the words back with spaces.  tsu and Termux's sudo
      // rely on this: they pass the script unquoted, so it arrives as many
      // argv entries.
      std::string joined;
      for (int j = i + 1; j < argc; ++j) {
        if (j > i + 1)
          joined.push_back(' ');
        if (argv[j] != nullptr)
          joined += argv[j];
      }
      command = joined;
      have_command = true;
      break; // the rest of the command line is the command
    } else if (arg.rfind("--command=", 0) == 0) {
      command = arg.substr(strlen("--command="));
      have_command = true;
    } else if (arg == "-s" || arg == "--shell") {
      if (i + 1 >= argc) {
        std::cerr << "su: option " << arg << " needs an argument" << std::endl;
        return 1;
      }
      shell = argv[++i];
    } else if (arg.rfind("--shell=", 0) == 0) {
      shell = arg.substr(strlen("--shell="));
    } else if (arg == "-u" || arg == "--user") {
      if (i + 1 >= argc) {
        std::cerr << "su: option " << arg << " needs an argument" << std::endl;
        return 1;
      }
      user_spec = argv[++i];
    } else if (arg.rfind("--user=", 0) == 0) {
      user_spec = arg.substr(strlen("--user="));
    } else if (arg == "-g" || arg == "--group") {
      if (i + 1 >= argc) {
        std::cerr << "su: option " << arg << " needs an argument" << std::endl;
        return 1;
      }
      group_spec = argv[++i];
    } else if (arg.rfind("--group=", 0) == 0) {
      group_spec = arg.substr(strlen("--group="));
    } else if (arg == "-G" || arg == "--supp-group") {
      if (i + 1 >= argc) {
        std::cerr << "su: option " << arg << " needs an argument" << std::endl;
        return 1;
      }
      supp_specs.push_back(argv[++i]);
    } else if (arg.rfind("--supp-group=", 0) == 0) {
      supp_specs.push_back(arg.substr(strlen("--supp-group=")));
    } else if (arg == "-p" || arg == "--preserve-environment") {
      preserve_env = true;
    } else if (arg == "-l" || arg == "--login" || arg == "-") {
      login = true;
    } else if (arg == "-M" || arg == "--mount-master" || arg == "-mm") {
      mount_master = true;
    } else if (arg == "-Z" || arg == "--context") {
      if (i + 1 >= argc) {
        std::cerr << "su: option " << arg << " needs an argument" << std::endl;
        return 1;
      }
      context = argv[++i];
    } else if (arg.rfind("--context=", 0) == 0) {
      context = arg.substr(strlen("--context="));
    } else if (arg == "--") {
      for (++i; i < argc; ++i)
        positional.push_back(argv[i] != nullptr ? argv[i] : "");
      break;
    } else if (arg == "-h" || arg == "--help") {
      print_usage();
      return 0;
    } else if (arg == "-v" || arg == "--version") {
      std::cout << "ncore su 1.0.0" << std::endl;
      return 0;
    } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
      std::cerr << "su: unknown option: " << arg << std::endl;
      return 1;
    } else {
      positional.push_back(arg);
    }
  }

  // The target user is either -u/--user or the first positional argument
  // (`su [options] [-] [user [argument...]]`).
  if (!positional.empty()) {
    if (!user_spec.empty() && user_spec != positional[0]) {
      std::cerr << "su: conflicting user specification" << std::endl;
      return 1;
    }
    user_spec = positional[0];
    positional.erase(positional.begin());
  }

  // Words left after the user are the command, joined the same way -c does.
  if (!have_command && !positional.empty()) {
    std::string joined;
    for (size_t j = 0; j < positional.size(); ++j) {
      if (j > 0)
        joined.push_back(' ');
      joined += positional[j];
    }
    command = joined;
    have_command = true;
  }

  // Identity to run as: the requested user, or whatever the kernel already
  // made us when no user was named (root in the nksu domain).
  Identity target;
  std::string error;
  if (!user_spec.empty()) {
    if (!resolve_user(user_spec, target, error)) {
      std::cerr << "su: " << error << std::endl;
      return 1;
    }
  } else {
    target.uid = ::geteuid();
    target.gid = ::getegid();
    set_name_home(target, ::getpwuid(target.uid), std::to_string(target.uid));
  }

  if (!group_spec.empty() && !resolve_group(group_spec, target.gid, error)) {
    std::cerr << "su: " << error << std::endl;
    return 1;
  }

  std::vector<gid_t> supp_groups;
  for (const std::string &spec : supp_specs) {
    gid_t gid = 0;
    if (!resolve_group(spec, gid, error)) {
      std::cerr << "su: " << error << std::endl;
      return 1;
    }
    supp_groups.push_back(gid);
  }

  if (shell.empty())
    shell = kDefaultShell;

  if (mount_master)
    switch_to_global_mount_ns();

  if (!context.empty())
    set_selinux_context(context);

  if (!drop_privileges(target, supp_groups, !supp_specs.empty(), error)) {
    std::cerr << "su: " << error << std::endl;
    return 1;
  }

  if (!preserve_env) {
    ::setenv("HOME", target.home.c_str(), 1);
    ::setenv("USER", target.name.c_str(), 1);
    ::setenv("LOGNAME", target.name.c_str(), 1);
    ::setenv("SHELL", shell.c_str(), 1);
  }

  prepend_path_dir(kNksuBinDir);
  prepend_path_dir(kKsuBinDir);
  prepend_path_dir(kNksuDir);
  ::umask(022);

  std::vector<char *> args;
  std::string arg0 = login ? "-" : shell;
  args.push_back(const_cast<char *>(arg0.c_str()));
  if (have_command) {
    args.push_back(const_cast<char *>("-c"));
    args.push_back(const_cast<char *>(command.c_str()));
  }
  args.push_back(nullptr);

  ::execv(shell.c_str(), args.data());
  std::cerr << "su: cannot execute " << shell << ": " << std::strerror(errno)
            << std::endl;
  return 127;
}

} // namespace su
