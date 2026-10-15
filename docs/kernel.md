**English** | [简体中文](kernel.zh.md)

# Kernel module developer guide

This page is a reference for developers working on `kernel/`. For the high-level picture, see [Architecture](architecture.md); for build instructions, see [How to build](building.md).

## Directory layout {#directory-layout}

```txt
kernel/
├── core.c                 module_init/exit, component registry & ordering
├── Makefile               out-of-tree kbuild wrapper (M=, IDIR=)
├── Kbuild                 object list, ccflags, version/commit embedding, LTO
├── Kconfig                CONFIG_NKSU, CONFIG_NKSU_DEBUG, CONFIG_NKSU_SYSCALL
├── include/
│   ├── nksu.h             version fallbacks, component-group API, nksu_version()
│   ├── klog.h             pr_fmt("ncore: ")
│   └── fmac.h             umbrella header pulling in every subsystem header
├── boot/                  runtime bootstrap (NOT boot-image patching)
│   ├── hook_init.c/.h     first-stage watcher: follow init, staged bring-up
│   ├── init_rc.c/.h       init.rc read/fstat proxy → exec ncore
│   └── safe_mode.c/.h     volume-up safe mode → safe-mode flag via ioctl
├── hook/
│   ├── handle.c/.h        steady-state syscall handlers
│   ├── syscall.c/.h       sys_call_table slot patch/restore
│   ├── dispatch.c/.h      per-syscall redirect dispatcher (shared entry)
│   ├── get_arg.c          get argv from user memory
│   ├── patch.c/.h         text patching via fixmap + stop_machine
│   └── lsm_hook.c/.h      runtime LSM security_hook_list patching
├── manager/
│   ├── manager.c/.h       app signature trust, ncore bootstrap, fsnotify rescan
│   ├── ioctl.c/.h         control fd + IOC_CMD protocol
│   ├── feature.c/.h       feature registry + /data/adb/nksu/feature persistence
│   └── abx.c/.h           Android Binary XML (ABX) parser for packages.xml
├── privilege/
│   ├── privilege.c/.h     privilege_desc + escalation engine
│   ├── profile.c/.h       RCU snapshot of uid profiles
│   ├── profile_store.c/.h /data/adb/nksu/allow.profile persistence
│   └── ns.c/.h            switch_to_init_ns()
├── spawn/spawn.c          kernel → userspace launcher
├── symbol/
│   ├── symbol.c/.h        kallsyms scanner/cache
│   └── symbol_compat.c/.h typed indirection for specific symbols
├── fd/
│   ├── anonfd.c           [fmac_shm] mmap-able page
│   ├── eventfd.c          [fmac_evt] eventfd signalling
│   └── shm_hash.c         jhash-based mmap write detector
└── selinux/
    ├── selinux.c/.h       domain/cred helpers, relabel, init poll
    ├── policy.c/.h        duplicate/restore live policy, static rule set
    ├── rule.c/.h          rule engine (allow/deny/xperm/type/attr/transitions)
    ├── rule_file.c        decoder for IOC_SET_SEPOLICY batches
    ├── hide.c/.h          selinuxfs f_op patching + setprocattr LSM hook
    └── engine.c/.h        vendored policy engine (≥ 6.6)
```

The kernel/userspace wire contract lives in the repository-root `uapi/` directory
(`uapi/ioctl.h`, `uapi/selinux.h`) and is included by the kernel, the manager and `ncore`.

## Initialization and lifecycle {#initialization}

`core.c` owns `module_init`/`module_exit` (`nekosu_init` / `nekosu_exit`) and a component registry:

```c
typedef struct {
    const char *name;
    int (*init)(void);
    void (*exit)(void);
} module_component_t;
```

Ordering matters:

1. `nksu_symbol_compat_init()` resolves every unexported symbol first.
2. Bring-up path:
   - **first stage** (loaded by `init`, `pid==1`) → `hook_init()` performs staged bring-up;
   - **late load** → `nekosu_init_all_components()` runs everything now.
3. Teardown runs components in reverse order, then `nksu_symbol_compat_exit()` and `nksu_ksym_cache_clear()`.

Component groups:

- `early_components[]` — safe mode (`nksu_safe_mode_init` / `nksu_safe_mode_exit`); armed at `second_stage`, before `/data` and the zygote.
- `selinux_components[]` — `init_selinux_hook` / `selinux_exit`.
- `feature_components[]`, in order: anonymous FD, uid profile, profile store, tracepoint hook (only when `!CONFIG_NKSU_SYSCALL`), manager scan, SELinux hide, syscall hook (`CONFIG_NKSU_SYSCALL`), and daemon fd last (keeps `prctl(204)` granting the control fd to root after the temporary watcher is gone).

`core.c` exposes `nksu_init_early_components()`, `nksu_exit_early_components()`, `nksu_init_selinux_components()`, `nksu_exit_selinux_components()`, `nksu_init_feature_components()`, `nksu_exit_feature_components()` (used by the staged boot watcher) and `nksu_version()`.

## boot — bootstrap, not image patching {#boot}

::: warning
`kernel/boot/` does not touch boot or vendor_boot images. Image flashing lives in the manager app / `ncore`. This directory is the runtime bootstrap.
:::

- `hook_init.c` — when loaded at first stage, installs a temporary syscall watcher for `__NR_execve`, `__NR_execveat`, `__NR_prctl`, follows `init` through `selinux_setup` → `second_stage`, and detects zygote (`-Xzygote`). A kthread `"nksu-init"` initializes the early and SELinux components at `INIT_SECOND_STAGE`, then feature components once zygote is seen, then tears down the watcher. It also lets root obtain the control fd via `NKSU_PRCTL_GET_DRIVER_FD` (204).
- `init_rc.c` — appends `nksu_rc[]` to `init`'s view of `/system/etc/init/hw/init.rc` (and legacy `/system/etc/init.rc`) by proxying `__NR_read` and `__NR_fstat`. The appended lines `exec u:r:nksu:s0 root -- /data/adb/nksu/ncore {post-fs-data|services|boot-completed}` run at the corresponding `on` triggers. `NKSU_NCORE_PATH = "/data/adb/nksu/ncore"`.
- `safe_mode.c` — input handler on `EV_KEY/KEY_VOLUMEUP`; 3 presses latch a "safe mode requested" flag reported by the control ioctl (`IOC_GET_SAFEMODE`). It does not write a marker or reboot: ncore queries the flag at each stage and creates `/data/adb/nksu/safe_module` itself (see userspace/ncore), then stops the watcher with `IOC_DISARM_SAFEMODE` at its last stage. The ioctl is used instead of a proc node because the control fd is anonymous, so SELinux path checks never apply.
- `manager/daemon.c` — installs a steady-state `__NR_prctl` slot hook (feature stage) that grants the control fd to root, so ncore can reach `IOC_*` at every stage, not just before zygote. The hook is removed when safe mode is disarmed (`IOC_DISARM_SAFEMODE`, at ncore's last stage) or at module unload.

## hook — interception {#hook}

### Steady-state handlers {#handlers}

`NKSU_SYSCALL_HANDLERS(X)` is the single source of truth mapping syscall numbers to handlers:

| Syscall | Handler | Behaviour |
| --- | --- | --- |
| `__NR_prctl` | `handle_prctl_hooks` | manager-gated: 201 → `[fmac_shm]`, 203 → `[fmac_ctl]` |
| `__NR_execve` | `hook__NR_execve` | path redirect |
| `__NR_execveat` | `hook__NR_execveat` | path redirect |
| `__NR_setresuid` | `hook__NR_setresuid` | credential handling for escalated processes |
| `__NR_faccessat` / `__NR_newfstatat` | `hook_path_at` | path redirect for `su`/`sh` |

`try_redirect_path()` rewrites `/system/bin/su` and `/system/bin/sh` to `REDIRECT_TARGET` (= `NKSU_NCORE_PATH`), then calls `privilege_escalate_from_profile(NULL)` and `nksu_relabel_tty_fds()`.

### Two interception strategies {#strategies}

- **Default (tracepoints)**, `!CONFIG_NKSU_SYSCALL`: `probe_sys_enter` on the `sys_enter` tracepoint (gated by `nksu_profile_has_uid`), `probe_sched_fork` on `sched_process_fork`, plus `mark_threads_by_pid()`.
- **`CONFIG_NKSU_SYSCALL`**: `dispatch.c` points selected syscall-table entries at one shared entry (`nksu_dispatch_fast`) that dispatches by syscall number through `virt_table`. Hooks are installed and removed per syscall (`nksu_redirect_syscall`, `nksu_register_handler`, `nksu_unhook_syscall`); there is no group restore.

Text patching (`patch.c`) uses `stop_machine()` + `set_fixmap_offset(FIX_TEXT_POKE0, …)` + `copy_to_kernel_nofault`. `syscall.c` tracks every slot it patched in a `hook_table[256]` so it can restore them all.

### LSM hooks {#lsm-hooks}

`nksu_lsm_hook()` finds a `security_hook_list` entry by function-pointer match and rewrites it; on kernels before 6.12 it walks `security_hook_heads`, on 6.12+ it uses `static_calls_table` and `__static_call_update`. `NKSU_LSM_HOOK_INIT(fn, name, hook)` builds the registration. `nksu_lsm_unhook()` restores the original.

## Control fd and ioctl ABI {#control-fd--ioctl-abi}

Defined once in the shared [`uapi/ioctl.h`](../uapi/ioctl.h), included by the kernel via `manager/ioctl.h`, by the manager's JNI layer and by `ncore`:

```c
#define IOC_MAGIC 'F'
#define IOC_CMD   _IOWR(IOC_MAGIC, 0, struct fmac_ioc)
struct fmac_ioc { unsigned int flag; unsigned int size; unsigned char data[]; };
```

`fmac_ctlfd_get()` returns an anonymous inode fd named `[fmac_ctl]`; `fmac_ioctl()` dispatches on `flag`.

| Flag | Value | Payload |
| --- | --- | --- |
| `IOC_GET_SHM` | 0 | none (returns the shm fd) |
| `IOC_BIND_EVT` | 1 | `int efd` |
| `IOC_CHK_WRITE` | 2 | `int changed` (out) |
| `IOC_ADD_UID` / `IOC_DEL_UID` | 3 / 4 | `u32 uid` |
| `IOC_HAS_UID` | 5 | `u32 uid` (in/out) |
| `IOC_SET_CAP` / `IOC_GET_CAP` / `IOC_DEL_CAP` | 6 / 7 / 8 | `u32 uid` + `u64 caps` |
| `IOC_SEL_ADD_RULE` | 9 | `src[64] tgt[64] cls[64] perm[64] effect[4] invert[4]` |
| `IOC_SET_PROFILE` | 10 | `uid[4] caps[8] domain[64] ns[4] root_uid[4]` |
| `IOC_SET_SEPOLICY` | 12 | KernelSU-format batch |
| `IOC_GET_PROFILES` | 13 | text out (`NKSU_PROFILE_TEXT_MAX = 64K`) |
| `IOC_GET_VERSION` | 14 | version string out (≤64) |
| `IOC_FEATURE_LIST` | 15 | text out (`NKSU_FEATURE_TEXT_MAX = 1024`) |
| `IOC_FEATURE_GET` / `IOC_FEATURE_SET` | 16 / 17 | `u32 id` (+ `u64 value`) |

Payload offsets (pinned with `static_assert`): `FMAC_OFF_UID 0`, `FMAC_OFF_CAPS 4`, `FMAC_OFF_DOMAIN 12`, `FMAC_OFF_NS 76`, `FMAC_OFF_ROOT_UID 80`, `FMAC_OFF_TGT 64`, `FMAC_OFF_CLS 128`, `FMAC_OFF_PERM 192`, `FMAC_OFF_EFFECT 256`, `FMAC_OFF_INVERT 260`, `FMAC_OFF_FEATURE_VALUE 4`.

### prctl opcodes {#prctl-opcodes}

| Opcode | Name | Gated by |
| --- | --- | --- |
| 201 | manager shm fd | `is_manager()` |
| 203 | manager ctl fd | `is_manager()` |
| `NKSU_PRCTL_GET_DRIVER_FD` = 204 | get `[fmac_ctl]` | root |

### Feature registry {#feature-registry}

`struct nksu_feature { u32 id; const char *name; int (*get)(u64*); int (*set)(u64); }` with a 16-slot registry. Feature state persists to `/data/adb/nksu/feature`. Currently: `NKSU_FEATURE_SELINUX_HIDE = 1`.

## manager — trust and bootstrap {#manager}

- Trust anchor `TARGET_PACKAGE "me.nekosu.aqnya"` and `TARGET_HASH` (SHA-256 of the signing cert). `verify_package_signature()` parses `/data/system/packages.xml` (this is where the ABX parser in `abx.c` is used) and compares cert hashes; `get_uid_from_packages_list()` reads `/data/system/packages.list`.
- `scan_and_apply()` records the manager UID, installs `ncore` and busybox from the app's `nativeLibraryPath` into `/data/adb/nksu/`, and sets the default profile.
- An fsnotify inode mark watches `/data/system/packages.xml` and defers a rescan via `task_work`.
- API: `appscan_init/exit`, `is_manager()`, `is_manager_uid()`, `is_ncore_ready()`, `nksu_kpath_mkdir_p()`.

## privilege — credentials and profiles {#privilege}

`struct privilege_desc` describes an escalation:

```c
struct privilege_desc {
    bool set_uidgid;
    uid_t uid, gid;
    kernel_cap_t caps_to_raise;
    const char *selinux_domain;
    bool disable_seccomp;
    bool switch_to_init_ns;
};
```

`privilege_escalate()` prepares creds, applies uid/gid, raises caps, sets the domain, commits, disables seccomp, and optionally switches to the init namespace. `privilege_escalate_from_profile()` is what the `execve`/`setresuid` hooks call.

`profile.c` keeps an RCU snapshot (`nksu_profile_blob`, magic `0x4b4e5355`) with a sorted entry array, a domain-string arena, and a 32768-bit `bitmap` as an O(1) membership fast path for low uids; uids above the bitmap are resolved by binary search, so the full 32-bit uid space (including Android per-user offsets) is supported. The default profile grants `CAP_FULL_SET`, domain `u:r:nksu:s0`, and `NKSU_NS_INHERITED`.

`profile_store.c` persists profiles to `/data/adb/nksu/allow.profile` in a line-based format:

```txt
# comments
<uid> <caps_hex> <ns> <root_uid> <selinux_domain>
```

`ns.h`: `NKSU_NS_INHERITED 1`, `NKSU_NS_GLOBAL 3`.

## selinux {#selinux}

- `selinux.c` — domain constants `DOMAIN "nksu"`, `DOMAIN_FILE "nksu_file"`, contexts `u:r:nksu:s0` / `u:object_r:nksu_file:s0`. Provides `setenforce`/`getenforce`, `set_domain`, `nksu_borrow_creds_begin/end/release` (a cached `CAP_FULL_SET` credential), `nksu_relabel_path` (in-memory SID + `security.selinux` xattr), and `nksu_relabel_tty_fds`.
- `policy.c` — `sepolicy_dup_and_apply()` deep-copies the live policy via `policydb_write/read` and RCU-swaps it, keeping `nksu_orig_policy` for `sepolicy_restore()`. `load_policy()` applies a static KernelSU-aligned rule set.
- `rule.c` — the rule engine: `sepolicy_add_rule/xperm/type/attribute/typeattribute/type_transition/type_change/type_member/genfscon`, `sepolicy_set_permissive/enforce`, `sepolicy_add_domain`, plus `avc_reset()`.
- `rule_file.c` — `sepolicy_apply_batch()` decodes the wire format below.
- `hide.c` — feature `"selinux_hide"` (id 1). Patches selinuxfs file ops and installs an LSM `setprocattr` hook so app UIDs (≥ 10000) see the clean original policy.
- `engine.c` — vendored policy engine used on kernels ≥ 6.6 (`nksu_engine_context_to_sid`, `nksu_engine_sid_to_context`, `nksu_engine_compute_av`).

### sepolicy wire format {#sepolicy-wire-format}

The shared [`uapi/selinux.h`](../uapi/selinux.h) (which mirrors KernelSU's wire format): each record is `<u32 cmd><u32 subcmd>` followed by up to 5 arguments encoded as `<u32 len><bytes><NUL>`, where `len == 0` means wildcard/ALL.

| cmd | Value |
| --- | --- |
| `NKSU_SEPOLICY_CMD_NORMAL_PERM` | 1 |
| `XPERM` | 2 |
| `TYPE_STATE` | 3 |
| `TYPE` | 4 |
| `TYPE_ATTR` | 5 |
| `ATTR` | 6 |
| `TYPE_TRANSITION` | 7 |
| `TYPE_CHANGE` | 8 |
| `GENFSCON` | 9 |

Limits: `NKSU_SEPOLICY_MAX_BATCH_SIZE` = 8 MiB, `NKSU_SEPOLICY_MAX_ARGS` = 5.

## symbol — resolving unexported symbols {#symbol}

The module never links unexported GKI symbols directly. `symbol.c` implements a self-contained kallsyms resolver `nksu_ksym_lookup(name)`: it tries `/proc/kallsyms` first, then scans kernel memory (bounded using `/sys/kernel/vmcoreinfo`), decodes compressed kallsyms via the token table and markers, and caches results in a hash table. `symbol_compat.c` declares typed pointers and `#define`s the original names to them; affected files drop CFI (`no_sanitize("cfi")`) so indirect calls cannot fail a type-hash check.

## fd and spawn {#fd-spawn}

- `fd/` provides the kernel → manager data channel: `[fmac_shm]` (a `vmalloc_user(PAGE_SIZE)` buffer mappable by the manager, in `anonfd.c`), `[fmac_evt]` (eventfd, in `eventfd.c`), and a `jhash`-based write detector (`shm_hash.c`).
- `spawn/spawn.c` launches userspace programs from the kernel without `call_usermodehelper`: a supervisor kthread `"nksu-spawn"` creates `nksu_spawn_child` via `kernel_thread()`, resets signals/creds/domains, waits for initramfs, then `kernel_execve()`.

## Coding conventions {#coding-conventions}

- Follow `.clang-format` (Linux kernel style, 120 columns, 4-space indent, function braces on a new line).
- New ABI additions belong in the shared `uapi/` headers (`uapi/ioctl.h`, `uapi/selinux.h`) and must be pinned with `static_assert`.
- Prefer extending `NKSU_SYSCALL_HANDLERS(X)` and the component registry rather than adding new entry points.
- Rebuild and bump the root `version` file whenever the kernel ABI or behaviour changes.
