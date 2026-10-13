#pragma once

// `su` for nksu.
//
// The kernel redirects an execve() of /system/bin/su to this binary and has
// already escalated the process according to the caller's profile by the time
// we run, so the current uid is the profile's uid (which may not be root).
// This interprets the su arguments and execs the shell; an explicit target
// user relies on the profile's CAP_SETUID/CAP_SETGID to switch identity.  It
// mirrors the parts of KernelSU's ksud `su`/`root_shell` that programs such as
// tsu and Termux's sudo rely on.
namespace su {

// True when the program was invoked as `su` (argv[0]'s basename is "su").
bool is_su_invocation(const char *arg0);

// Run the `su` command. Returns the process exit code (only on failure; on
// success it does not return because it execs the shell).
int run(int argc, char *argv[]);

} // namespace su
