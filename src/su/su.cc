#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "su/su.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <sched.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace su {

namespace {

constexpr const char *kDefaultShell = "/system/bin/sh";
constexpr const char *kNksuBinDir = "/data/adb/nksu";

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
               "  -p, --preserve-environment   keep the current environment\n"
               "  -l, --login                  start a login shell\n"
               "  -M, --mount-master           run in the global mount namespace\n"
               "  -u, --user USER              run as USER (name or uid)\n"
               "  -g, --group GROUP            use GROUP as the primary group\n"
               "  -G, --supp-group GROUP       add GROUP to the supplementary groups\n"
               "  -Z, --context CONTEXT        set the SELinux context\n"
               "  -v, --version                show version\n"
               "  -h, --help                   show this help\n"
            << std::endl;
}

// Append a directory to PATH if it is not there yet.
void prepend_path_dir(const std::string &dir) {
  const char *path_env = ::getenv("PATH");
  const std::string path =
      path_env != nullptr ? path_env : "/sbin:/system/sbin:/system/bin:/system/xbin";
  if (path.find(dir) != std::string::npos)
    return;
  const std::string combined = dir + ":" + path;
  ::setenv("PATH", combined.c_str(), 1);
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

// Resolve a user name or a decimal uid.  Returns false when it is neither a
// valid number nor a known account.
bool resolve_uid(const std::string &user, uid_t &out) {
  if (user.empty())
    return false;

  char *end = nullptr;
  const unsigned long value = std::strtoul(user.c_str(), &end, 10);
  if (end != nullptr && *end == '\0') {
    out = static_cast<uid_t>(value);
    return true;
  }

  const struct passwd *pw = ::getpwnam(user.c_str());
  if (pw != nullptr) {
    out = pw->pw_uid;
    return true;
  }
  return false;
}

// Resolve a group name or a decimal gid.
bool resolve_gid(const std::string &group, gid_t &out) {
  if (group.empty())
    return false;

  char *end = nullptr;
  const unsigned long value = std::strtoul(group.c_str(), &end, 10);
  if (end != nullptr && *end == '\0') {
    out = static_cast<gid_t>(value);
    return true;
  }

  const struct group *gr = ::getgrnam(group.c_str());
  if (gr != nullptr) {
    out = gr->gr_gid;
    return true;
  }
  return false;
}

// Switch to the requested uid/gid.  The kernel has already applied the root
// profile, so this only runs when the caller asked for an explicit identity and
// relies on the profile's CAP_SETUID/CAP_SETGID.
int set_identity(uid_t uid, gid_t gid, const std::vector<gid_t> &groups) {
  if (::setgroups(groups.size(), groups.empty() ? nullptr : groups.data()) != 0) {
    std::cerr << "su: setgroups: " << std::strerror(errno) << std::endl;
    return 1;
  }
  if (::setresgid(gid, gid, gid) != 0) {
    std::cerr << "su: setresgid " << gid << ": " << std::strerror(errno) << std::endl;
    return 1;
  }
  if (::setresuid(uid, uid, uid) != 0) {
    std::cerr << "su: setresuid " << uid << ": " << std::strerror(errno) << std::endl;
    return 1;
  }
  return 0;
}

// Match `--option=value` and copy the value out.
bool inline_option(const std::string &arg, const char *name, std::string &out) {
  const size_t len = std::strlen(name);
  if (arg.rfind(name, 0) != 0 || arg.size() <= len || arg[len] != '=')
    return false;
  out = arg.substr(len + 1);
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
  std::string user;
  bool have_user = false;
  bool have_command = false;
  bool preserve_env = false;
  bool login = false;
  bool mount_master = false;
  bool have_gid = false;
  gid_t primary_gid = 0;
  std::vector<gid_t> supp_groups;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] != nullptr ? argv[i] : "";
    std::string value;

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
    } else if (arg == "-u" || arg == "--user" || inline_option(arg, "--user", value)) {
      if (value.empty()) {
        if (i + 1 >= argc) {
          std::cerr << "su: option " << arg << " needs an argument" << std::endl;
          return 1;
        }
        value = argv[++i];
      }
      user = value;
      have_user = true;
    } else if (arg == "-g" || arg == "--group" || inline_option(arg, "--group", value)) {
      if (value.empty()) {
        if (i + 1 >= argc) {
          std::cerr << "su: option " << arg << " needs an argument" << std::endl;
          return 1;
        }
        value = argv[++i];
      }
      if (!resolve_gid(value, primary_gid)) {
        std::cerr << "su: unknown group: " << value << std::endl;
        return 1;
      }
      have_gid = true;
    } else if (arg == "-G" || arg == "--supp-group" || inline_option(arg, "--supp-group", value)) {
      if (value.empty()) {
        if (i + 1 >= argc) {
          std::cerr << "su: option " << arg << " needs an argument" << std::endl;
          return 1;
        }
        value = argv[++i];
      }
      gid_t gid = 0;
      if (!resolve_gid(value, gid)) {
        std::cerr << "su: unknown group: " << value << std::endl;
        return 1;
      }
      supp_groups.push_back(gid);
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

  // The kernel has already applied the root profile, so the process is running
  // as the profile's uid (which may be non-root).  An explicit positional user
  // overrides it.
  if (!positional.empty()) {
    user = positional[0];
    have_user = true;
  }

  uid_t target_uid = ::getuid();
  if (have_user && !resolve_uid(user, target_uid)) {
    std::cerr << "su: unknown user: " << user << std::endl;
    return 1;
  }

  const gid_t target_gid = have_gid ? primary_gid : target_uid;
  const bool identity_requested = have_user || have_gid || !supp_groups.empty();

  if (shell.empty())
    shell = kDefaultShell;

  if (mount_master)
    switch_to_global_mount_ns();

  if (!preserve_env) {
    const struct passwd *pw = ::getpwuid(target_uid);
    const std::string home = (pw != nullptr && pw->pw_dir != nullptr) ? pw->pw_dir : "/";
    const std::string name =
        (pw != nullptr && pw->pw_name != nullptr) ? pw->pw_name : std::to_string(target_uid);
    ::setenv("HOME", home.c_str(), 1);
    ::setenv("USER", name.c_str(), 1);
    ::setenv("LOGNAME", name.c_str(), 1);
    ::setenv("SHELL", shell.c_str(), 1);
  }

  prepend_path_dir(kNksuBinDir);
  ::umask(022);

  if (!context.empty())
    set_selinux_context(context);

  if (identity_requested && set_identity(target_uid, target_gid, supp_groups) != 0)
    return 1;

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
