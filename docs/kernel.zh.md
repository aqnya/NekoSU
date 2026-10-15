[English](kernel.md) | **简体中文**

# 内核模块开发者指南

本页是 `kernel/` 开发者的参考。整体结构见[架构](architecture.zh.md)；构建说明见[如何构建](building.zh.md)。

## 目录结构 {#directory-layout}

```txt
kernel/
├── core.c                 module_init/exit，组件注册与顺序
├── Makefile               树外 kbuild 包装（M=、IDIR=）
├── Kbuild                 目标列表、ccflags、版本/提交嵌入、LTO
├── Kconfig                CONFIG_NKSU、CONFIG_NKSU_DEBUG、CONFIG_NKSU_SYSCALL
├── include/
│   ├── nksu.h             版本回退、组件组 API、nksu_version()
│   ├── klog.h             pr_fmt("ncore: ")
│   └── fmac.h             引入所有子系统头文件的伞头文件
├── boot/                  运行时引导（不是 boot 镜像修补）
│   ├── hook_init.c/.h     一阶段观察器：跟随 init、分阶段初始化
│   ├── init_rc.c/.h       init.rc 读/fstat 代理 → exec ncore
│   └── safe_mode.c/.h     音量键安全模式 → 经 ioctl 的安全模式标志
├── hook/
│   ├── handle.c/.h        稳态系统调用处理函数
│   ├── syscall.c/.h       sys_call_table 槽位修补/恢复
│   ├── dispatch.c/.h      单系统调用重定向分发器（共享入口）
│   ├── get_arg.c          从用户内存读取 argv
│   ├── patch.c/.h         通过 fixmap + stop_machine 修补文本
│   └── lsm_hook.c/.h      运行时修补 LSM security_hook_list
├── manager/
│   ├── manager.c/.h       应用签名信任、ncore 引导、fsnotify 重扫描
│   ├── ioctl.c/.h         控制 fd + IOC_CMD 协议
│   ├── feature.c/.h       特性注册表 + /data/adb/nksu/feature 持久化
│   └── abx.c/.h           用于 packages.xml 的 Android 二进制 XML（ABX）解析器
├── privilege/
│   ├── privilege.c/.h     privilege_desc + 提权引擎
│   ├── profile.c/.h       uid 档案的 RCU 快照
│   ├── profile_store.c/.h /data/adb/nksu/allow.profile 持久化
│   └── ns.c/.h            switch_to_init_ns()
├── spawn/spawn.c          内核 → 用户态启动器
├── symbol/
│   ├── symbol.c/.h        kallsyms 扫描/缓存
│   └── symbol_compat.c/.h 特定符号的类型化间接调用
├── fd/
│   ├── anonfd.c           [fmac_shm] 可 mmap 页
│   ├── eventfd.c          [fmac_evt] eventfd 通知
│   └── shm_hash.c         基于 jhash 的 mmap 写入检测器
└── selinux/
    ├── selinux.c/.h       域/凭证辅助、重打标签、init 轮询
    ├── policy.c/.h        复制/恢复活动策略、静态规则集
    ├── rule.c/.h          规则引擎（allow/deny/xperm/type/attr/transitions）
    ├── rule_file.c        IOC_SET_SEPOLICY 批次解码器
    ├── hide.c/.h          selinuxfs f_op 修补 + setprocattr LSM hook
    └── engine.c/.h        内置策略引擎（≥ 6.6）
```

内核/用户态的线上契约位于仓库根目录的 `uapi/`（`uapi/ioctl.h`、`uapi/selinux.h`），
由内核、管理器与 `ncore` 共同包含。

## 初始化与生命周期 {#initialization}

`core.c` 负责 `module_init`/`module_exit`（`nekosu_init` / `nekosu_exit`）以及组件注册表：

```c
typedef struct {
    const char *name;
    int (*init)(void);
    void (*exit)(void);
} module_component_t;
```

顺序很重要：

1. `nksu_symbol_compat_init()` 先解析全部未导出符号。
2. 初始化路径：
   - **一阶段**（由 `init` 加载，`pid==1`）→ `hook_init()` 执行分阶段初始化；
   - **后期加载** → `nekosu_init_all_components()` 立即运行全部组件。
3. 拆除时按逆序运行组件，然后 `nksu_symbol_compat_exit()` 与 `nksu_ksym_cache_clear()`。

组件组：

- `early_components[]` —— 安全模式（`nksu_safe_mode_init` / `nksu_safe_mode_exit`）；在 `second_stage` 武装，早于 `/data` 与 zygote。
- `selinux_components[]` —— `init_selinux_hook` / `selinux_exit`。
- `feature_components[]`，按顺序：匿名 FD、uid 档案、档案存储、tracepoint hook（仅在 `!CONFIG_NKSU_SYSCALL` 时）、管理器扫描、SELinux 隐藏、系统调用 hook（`CONFIG_NKSU_SYSCALL`），最后是 daemon fd（在临时观察器拆除后继续让 root 通过 `prctl(204)` 拿到控制 fd）。

`core.c` 导出 `nksu_init_early_components()`、`nksu_exit_early_components()`、`nksu_init_selinux_components()`、`nksu_exit_selinux_components()`、`nksu_init_feature_components()`、`nksu_exit_feature_components()`（供分阶段引导观察器使用）与 `nksu_version()`。

## boot —— 引导，而非镜像修补 {#boot}

::: warning
`kernel/boot/` 不接触 boot 或 vendor_boot 镜像。镜像刷写位于管理器 App / `ncore`。本目录是运行时引导。
:::

- `hook_init.c` —— 在一阶段加载时，为 `__NR_execve`、`__NR_execveat`、`__NR_prctl` 安装临时系统调用观察器，跟随 `init` 经历 `selinux_setup` → `second_stage`，并检测 zygote（`-Xzygote`）。kthread `"nksu-init"` 在 `INIT_SECOND_STAGE` 初始化早期与 SELinux 组件，看到 zygote 后初始化特性组件，然后拆除观察器。它还允许 root 通过 `NKSU_PRCTL_GET_DRIVER_FD`（204）获取控制 fd。
- `init_rc.c` —— 通过代理 `__NR_read` 与 `__NR_fstat`，把 `nksu_rc[]` 追加到 `init` 可见的 `/system/etc/init/hw/init.rc`（以及旧的 `/system/etc/init.rc`）。追加的行 `exec u:r:nksu:s0 root -- /data/adb/nksu/ncore {post-fs-data|services|boot-completed}` 在对应的 `on` 触发器执行。`NKSU_NCORE_PATH = "/data/adb/nksu/ncore"`。
- `safe_mode.c` —— 针对 `EV_KEY/KEY_VOLUMEUP` 的输入处理；按 3 次会锁存一个“安全模式请求”标志，由控制 ioctl（`IOC_GET_SAFEMODE`）上报。它不写标记也不重启：ncore 在每个阶段查询该标志并自行创建 `/data/adb/nksu/safe_module`（见 userspace/ncore），最后在末阶段用 `IOC_DISARM_SAFEMODE` 停止观察器。用 ioctl 而非 proc 节点，是因为控制 fd 是匿名 inode，SELinux 路径检查不会介入。
- `manager/daemon.c` —— 在 feature 阶段安装一个稳态 `__NR_prctl` slot hook，把控制 fd 授予 root，使 ncore 在每个阶段（不止 zygote 前）都能访问 `IOC_*`。该 hook 在安全模式被解除时（`IOC_DISARM_SAFEMODE`，ncore 末阶段）或模块卸载时移除。

## hook —— 拦截 {#hook}

### 稳态处理函数 {#handlers}

`NKSU_SYSCALL_HANDLERS(X)` 是系统调用号到处理函数映射的唯一来源：

| 系统调用 | 处理函数 | 行为 |
| --- | --- | --- |
| `__NR_prctl` | `handle_prctl_hooks` | 受管理器门控：201 → `[fmac_shm]`，203 → `[fmac_ctl]` |
| `__NR_execve` | `hook__NR_execve` | 路径重定向 |
| `__NR_execveat` | `hook__NR_execveat` | 路径重定向 |
| `__NR_setresuid` | `hook__NR_setresuid` | 已提权进程的凭证处理 |
| `__NR_faccessat` / `__NR_newfstatat` | `hook_path_at` | `su`/`sh` 的路径重定向 |

`try_redirect_path()` 把 `/system/bin/su` 与 `/system/bin/sh` 重写为 `REDIRECT_TARGET`（= `NKSU_NCORE_PATH`），然后调用 `privilege_escalate_from_profile(NULL)` 与 `nksu_relabel_tty_fds()`。

### 两种拦截策略 {#strategies}

- **默认（tracepoint）**，`!CONFIG_NKSU_SYSCALL`：`sys_enter` tracepoint 上的 `probe_sys_enter`（受 `nksu_profile_has_uid` 门控）、`sched_process_fork` 上的 `probe_sched_fork`，以及 `mark_threads_by_pid()`。
- **`CONFIG_NKSU_SYSCALL`**：`dispatch.c` 把选定的系统调用表条目指向单一共享入口（`nksu_dispatch_fast`），按系统调用号经 `virt_table` 分发。每个系统调用独立安装/移除 hook（`nksu_redirect_syscall`、`nksu_register_handler`、`nksu_unhook_syscall`），没有统一还原。

文本修补（`patch.c`）使用 `stop_machine()` + `set_fixmap_offset(FIX_TEXT_POKE0, …)` + `copy_to_kernel_nofault`。`syscall.c` 在 `hook_table[256]` 中记录它修补过的每个槽位，以便全部恢复。

### LSM hook {#lsm-hooks}

`nksu_lsm_hook()` 通过函数指针匹配找到 `security_hook_list` 条目并改写它；在 6.12 之前的内核上遍历 `security_hook_heads`，6.12+ 使用 `static_calls_table` 与 `__static_call_update`。`NKSU_LSM_HOOK_INIT(fn, name, hook)` 构建注册项。`nksu_lsm_unhook()` 恢复原值。

## 控制 fd 与 ioctl ABI {#control-fd--ioctl-abi}

统一定义于共享的 [`uapi/ioctl.h`](../uapi/ioctl.h)，内核经 `manager/ioctl.h`、管理器的 JNI 层与 `ncore` 均包含它：

```c
#define IOC_MAGIC 'F'
#define IOC_CMD   _IOWR(IOC_MAGIC, 0, struct fmac_ioc)
struct fmac_ioc { unsigned int flag; unsigned int size; unsigned char data[]; };
```

`fmac_ctlfd_get()` 返回名为 `[fmac_ctl]` 的匿名 inode fd；`fmac_ioctl()` 按 `flag` 分发。

| 标志 | 值 | 载荷 |
| --- | --- | --- |
| `IOC_GET_SHM` | 0 | 无（返回 shm fd） |
| `IOC_BIND_EVT` | 1 | `int efd` |
| `IOC_CHK_WRITE` | 2 | `int changed`（输出） |
| `IOC_ADD_UID` / `IOC_DEL_UID` | 3 / 4 | `u32 uid` |
| `IOC_HAS_UID` | 5 | `u32 uid`（输入/输出） |
| `IOC_SET_CAP` / `IOC_GET_CAP` / `IOC_DEL_CAP` | 6 / 7 / 8 | `u32 uid` + `u64 caps` |
| `IOC_SEL_ADD_RULE` | 9 | `src[64] tgt[64] cls[64] perm[64] effect[4] invert[4]` |
| `IOC_SET_PROFILE` | 10 | `uid[4] caps[8] domain[64] ns[4] root_uid[4]` |
| `IOC_SET_SEPOLICY` | 12 | KernelSU 格式批次 |
| `IOC_GET_PROFILES` | 13 | 文本输出（`NKSU_PROFILE_TEXT_MAX = 64K`） |
| `IOC_GET_VERSION` | 14 | 版本字符串输出（≤64） |
| `IOC_FEATURE_LIST` | 15 | 文本输出（`NKSU_FEATURE_TEXT_MAX = 1024`） |
| `IOC_FEATURE_GET` / `IOC_FEATURE_SET` | 16 / 17 | `u32 id`（+ `u64 value`） |

载荷偏移（由 `static_assert` 固定）：`FMAC_OFF_UID 0`、`FMAC_OFF_CAPS 4`、`FMAC_OFF_DOMAIN 12`、`FMAC_OFF_NS 76`、`FMAC_OFF_ROOT_UID 80`、`FMAC_OFF_TGT 64`、`FMAC_OFF_CLS 128`、`FMAC_OFF_PERM 192`、`FMAC_OFF_EFFECT 256`、`FMAC_OFF_INVERT 260`、`FMAC_OFF_FEATURE_VALUE 4`。

### prctl 操作码 {#prctl-opcodes}

| 操作码 | 名称 | 门控 |
| --- | --- | --- |
| 201 | 管理器 shm fd | `is_manager()` |
| 203 | 管理器 ctl fd | `is_manager()` |
| `NKSU_PRCTL_GET_DRIVER_FD` = 204 | 获取 `[fmac_ctl]` | root |

### 特性注册表 {#feature-registry}

`struct nksu_feature { u32 id; const char *name; int (*get)(u64*); int (*set)(u64); }`，16 槽注册表。特性状态持久化到 `/data/adb/nksu/feature`。当前有 `NKSU_FEATURE_SELINUX_HIDE = 1`。

## manager —— 信任与引导 {#manager}

- 信任锚点 `TARGET_PACKAGE "me.nekosu.aqnya"` 与 `TARGET_HASH`（签名证书的 SHA-256）。`verify_package_signature()` 解析 `/data/system/packages.xml`（此处使用 `abx.c` 中的 ABX 解析器）并比对证书哈希；`get_uid_from_packages_list()` 读取 `/data/system/packages.list`。
- `scan_and_apply()` 记录管理器 UID，从 App 的 `nativeLibraryPath` 把 `ncore` 与 busybox 安装到 `/data/adb/nksu/`，并设置默认档案。
- fsnotify inode 监视 `/data/system/packages.xml`，通过 `task_work` 延迟重扫描。
- API：`appscan_init/exit`、`is_manager()`、`is_manager_uid()`、`is_ncore_ready()`、`nksu_kpath_mkdir_p()`。

## privilege —— 凭证与档案 {#privilege}

`struct privilege_desc` 描述一次提权：

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

`privilege_escalate()` 准备凭证、应用 uid/gid、提升能力、设置域、提交、禁用 seccomp，并按需切换到 init 命名空间。`privilege_escalate_from_profile()` 是 `execve`/`setresuid` hook 调用的函数。

`profile.c` 维护一个 RCU 快照（`nksu_profile_blob`，magic `0x4b4e5355`），含有序条目数组、域字符串区，以及作为低 UID 段 O(1) 成员判断快速路径的 32768 位 `bitmap`；超过该位图的 UID 走二分查找，因此支持完整的 32 位 UID 空间（含 Android 多用户偏移）。默认档案授予 `CAP_FULL_SET`、域 `u:r:nksu:s0` 与 `NKSU_NS_INHERITED`。

`profile_store.c` 把档案持久化到 `/data/adb/nksu/allow.profile`，采用逐行格式：

```txt
# 注释
<uid> <caps_hex> <ns> <root_uid> <selinux_domain>
```

`ns.h`：`NKSU_NS_INHERITED 1`、`NKSU_NS_GLOBAL 3`。

## selinux {#selinux}

- `selinux.c` —— 域常量 `DOMAIN "nksu"`、`DOMAIN_FILE "nksu_file"`，上下文 `u:r:nksu:s0` / `u:object_r:nksu_file:s0`。提供 `setenforce`/`getenforce`、`set_domain`、`nksu_borrow_creds_begin/end/release`（缓存的 `CAP_FULL_SET` 凭证）、`nksu_relabel_path`（内存 SID + `security.selinux` xattr）、`nksu_relabel_tty_fds`。
- `policy.c` —— `sepolicy_dup_and_apply()` 通过 `policydb_write/read` 深拷贝活动策略并做 RCU 替换，保留 `nksu_orig_policy` 供 `sepolicy_restore()` 使用。`load_policy()` 应用一套与 KernelSU 对齐的静态规则。
- `rule.c` —— 规则引擎：`sepolicy_add_rule/xperm/type/attribute/typeattribute/type_transition/type_change/type_member/genfscon`、`sepolicy_set_permissive/enforce`、`sepolicy_add_domain`，以及 `avc_reset()`。
- `rule_file.c` —— `sepolicy_apply_batch()` 解码下方线上格式。
- `hide.c` —— 特性 `"selinux_hide"`（id 1）。修补 selinuxfs 文件操作并安装 LSM `setprocattr` hook，使应用 UID（≥ 10000）看到干净的原始策略。
- `engine.c` —— 用于内核 ≥ 6.6 的内置策略引擎（`nksu_engine_context_to_sid`、`nksu_engine_sid_to_context`、`nksu_engine_compute_av`）。

### sepolicy 线上格式 {#sepolicy-wire-format}

共享的 [`uapi/selinux.h`](../uapi/selinux.h)（线上格式与 KernelSU 一致）：每条记录为 `<u32 cmd><u32 subcmd>`，后跟最多 5 个参数，参数编码为 `<u32 len><bytes><NUL>`，`len == 0` 表示通配/ALL。

| cmd | 值 |
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

限制：`NKSU_SEPOLICY_MAX_BATCH_SIZE` = 8 MiB，`NKSU_SEPOLICY_MAX_ARGS` = 5。

## symbol —— 解析未导出符号 {#symbol}

模块从不直接链接未导出的 GKI 符号。`symbol.c` 实现自包含的 kallsyms 解析器 `nksu_ksym_lookup(name)`：先尝试 `/proc/kallsyms`，再扫描内核内存（用 `/sys/kernel/vmcoreinfo` 限定范围），通过 token 表与标记解码压缩的 kallsyms，并把结果缓存在哈希表中。`symbol_compat.c` 声明类型化指针并用 `#define` 把原名称指向它们；受影响的文件丢弃 CFI（`no_sanitize("cfi")`），使间接调用不会因类型哈希检查而失败。

## fd 与 spawn {#fd-spawn}

- `fd/` 提供内核 → 管理器的数据通道：`[fmac_shm]`（一个 `vmalloc_user(PAGE_SIZE)` 缓冲区，管理器可 mmap，位于 `anonfd.c`）、`[fmac_evt]`（eventfd，位于 `eventfd.c`），以及基于 `jhash` 的写入检测器（`shm_hash.c`）。
- `spawn/spawn.c` 不借助 `call_usermodehelper` 从内核启动用户态程序：监督 kthread `"nksu-spawn"` 通过 `kernel_thread()` 创建 `nksu_spawn_child`，重置信号/凭证/域，等待 initramfs，然后 `kernel_execve()`。

## 编码约定 {#coding-conventions}

- 遵循 `.clang-format`（Linux 内核风格，120 列，4 空格缩进，函数大括号另起一行）。
- 新增 ABI 应放入共享的 `uapi/` 头文件（`uapi/ioctl.h`、`uapi/selinux.h`），并用 `static_assert` 固定。
- 优先扩展 `NKSU_SYSCALL_HANDLERS(X)` 与组件注册表，而不是新增入口点。
- 每当内核 ABI 或行为变化时，重新构建并递增根目录 `version` 文件。
