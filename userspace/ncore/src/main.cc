#include "cli.hpp"
#include "prop.hpp"
#include "su/su.hpp"

#include <cstring>

namespace {
// True when the last path component of `path` is `name`.
bool argv0_is(const char *path, const char *name) {
  if (path == nullptr)
    return false;
  const char *slash = std::strrchr(path, '/');
  return std::strcmp(slash ? slash + 1 : path, name) == 0;
}
} // namespace

int main(int argc, char **argv) {
  // The kernel redirects an execve() of /system/bin/su to this binary, keeping
  // argv[0] intact, so detect the `su` invocation by argv[0] (or the explicit
  // `ncore su` subcommand).
  if (su::is_su_invocation(argc > 0 ? argv[0] : nullptr))
    return su::run(argc, argv);

  if (argc > 1 && std::strcmp(argv[1], "su") == 0)
    return su::run(argc - 1, argv + 1);

  // `resetprop` is a symlink to this binary (bin/resetprop), so detect it by
  // argv[0] as well as the explicit `ncore resetprop` subcommand.
  if (argv0_is(argc > 0 ? argv[0] : nullptr, "resetprop"))
    return resetprop_main(argc, argv);

  if (argc > 1 && std::strcmp(argv[1], "resetprop") == 0)
    return resetprop_main(argc - 1, argv + 1);

  cli::arguments args;
  const int rc = cli::parse_cli(argc, argv, args);
  if (rc != 0)
    return rc;

  return 0;
}
