#include <android/log.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <sys/system_properties.h>

#include <uapi/ioctl.h>

#include "log.h"

enum Opcode {
  OP_AUTHENTICATE = NKSU_PRCTL_GET_SHM_FD,
  OP_IOCTL = NKSU_PRCTL_GET_CTL_FD,
};

class JniUtfString {
public:
  JniUtfString(JNIEnv *env, jstring jstr)
      : env_(env), jstr_(jstr), cstr_(nullptr) {
    if (jstr_ != nullptr) {
      cstr_ = env_->GetStringUTFChars(jstr_, nullptr);
    }
  }
  ~JniUtfString() {
    if (cstr_ != nullptr) {
      env_->ReleaseStringUTFChars(jstr_, cstr_);
    }
  }
  JniUtfString(const JniUtfString &) = delete;
  JniUtfString &operator=(const JniUtfString &) = delete;

  const char *c_str() const { return cstr_; }
  const char *c_str_or_empty() const { return cstr_ ? cstr_ : ""; }
  explicit operator bool() const { return cstr_ != nullptr; }

private:
  JNIEnv *env_;
  jstring jstr_;
  const char *cstr_;
};

#define FMAC_MAX_DATA 4096

namespace jni {
namespace {

inline void copy_field(void *dst, size_t cap, const char *s) {
  snprintf(static_cast<char *>(dst), cap, "%s", s ? s : "");
}

static int ioc_call(int fd, unsigned int flag, void *data, size_t size) {
  union {
    struct fmac_ioc msg;
    uint8_t raw[sizeof(struct fmac_ioc) + FMAC_MAX_DATA];
  } u;
  int ret;

  if (size > FMAC_MAX_DATA) {
    errno = EINVAL;
    return -1;
  }

  // Zero the header and the payload we actually use, so no stack bytes leak
  // into the kernel and the tail stays deterministic.
  memset(&u, 0, sizeof(struct fmac_ioc) + size);
  u.msg.flag = flag;
  u.msg.size = (uint32_t)size;
  if (size)
    memcpy(u.msg.data, data, size);

  ret = ioctl(fd, IOC_CMD, &u);
  /*
   * Copy the payload back on any non-negative return.  Most flags return 0 on
   * success, but IOC_FEATURE_LIST / IOC_GET_PROFILES return the number of bytes
   * they wrote, so testing for 0 dropped the whole answer and looked like "the
   * kernel reported no features".
   */
  if (ret >= 0 && size)
    memcpy(data, u.msg.data, size);

  return ret;
}

static int scan_fd_by_link(const char *target) {
  DIR *dir;
  struct dirent *ent;
  char path[64];
  char link[256];
  int fdnum;

  dir = opendir("/proc/self/fd");
  if (!dir)
    return -1;

  errno = 0;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;

    fdnum = atoi(ent->d_name);
    snprintf(path, sizeof(path), "/proc/self/fd/%s", ent->d_name);

    ssize_t len = readlink(path, link, sizeof(link) - 1);
    if (len < 0)
      continue;
    link[len] = '\0';

    if (strstr(link, target)) {
      closedir(dir);
      return fdnum;
    }
  }

  closedir(dir);
  return -1;
}

int Ctl(enum Opcode code) {
  switch (code) {
  case OP_AUTHENTICATE:
  case OP_IOCTL:
    return prctl((unsigned int)code, 0, 0, 0, 0);
  default:
    errno = EINVAL;
    return -1;
  }
}

int SetProfile(int fd, int uid, int root_uid, uint64_t caps, const char *domain, int ns) {
  uint8_t data[FMAC_DATA_PROFILE];
  uint32_t u = (uint32_t)uid;
  uint32_t ru = (uint32_t)root_uid;

  memset(data, 0, sizeof(data));
  memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
  memcpy(data + FMAC_OFF_CAPS, &caps, sizeof(caps));
  copy_field(data + FMAC_OFF_DOMAIN, FMAC_OFF_NS - FMAC_OFF_DOMAIN, domain);
  memcpy(data + FMAC_OFF_NS, &ns, sizeof(ns));
  memcpy(data + FMAC_OFF_ROOT_UID, &ru, sizeof(ru));

  return ioc_call(fd, IOC_SET_PROFILE, data, sizeof(data));
}

int AddUid(int fd, int uid) {
  if (uid < 0) {
    errno = EINVAL;
    return -1;
  }
  uint32_t val = (uint32_t)uid;
  return ioc_call(fd, IOC_ADD_UID, &val, sizeof(val));
}

int DelUid(int fd, int uid) {
  if (uid < 0) {
    errno = EINVAL;
    return -1;
  }
  uint32_t val = (uint32_t)uid;
  return ioc_call(fd, IOC_DEL_UID, &val, sizeof(val));
}

int HasUid(int fd, int uid, int *has) {
  if (uid < 0 || !has) {
    errno = EINVAL;
    return -1;
  }
  uint32_t val = (uint32_t)uid;
  if (ioc_call(fd, IOC_HAS_UID, &val, sizeof(val)) < 0)
    return -1;
  *has = (val != 0);
  return 0;
}

int SetCap(int fd, int uid, uint64_t caps) {
  uint8_t data[FMAC_DATA_CAP];
  uint32_t u = (uint32_t)uid;

  memset(data, 0, sizeof(data));
  memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
  memcpy(data + FMAC_OFF_CAPS, &caps, sizeof(caps));

  return ioc_call(fd, IOC_SET_CAP, data, sizeof(data));
}

int GetCap(int fd, int uid, uint64_t *caps) {
  uint8_t data[FMAC_DATA_CAP];
  uint32_t u = (uint32_t)uid;

  if (!caps) {
    errno = EINVAL;
    return -1;
  }

  memset(data, 0, sizeof(data));
  memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
  if (ioc_call(fd, IOC_GET_CAP, data, sizeof(data)) < 0)
    return -1;
  memcpy(caps, data + FMAC_OFF_CAPS, sizeof(*caps));
  return 0;
}

int DelCap(int fd, int uid) {
  uint8_t data[FMAC_DATA_CAP];
  uint32_t u = (uint32_t)uid;

  memset(data, 0, sizeof(data));
  memcpy(data + FMAC_OFF_UID, &u, sizeof(u));

  return ioc_call(fd, IOC_DEL_CAP, data, sizeof(data));
}

int AddSelinuxRule(int fd, const char *src, const char *tgt, const char *cls,
                   const char *perm, int effect, int invert) {
  uint8_t data[FMAC_DATA_SELRULE];
  int inv = invert ? 1 : 0;

  memset(data, 0, sizeof(data));
  copy_field(data + FMAC_OFF_UID, FMAC_OFF_TGT - FMAC_OFF_UID, src);
  copy_field(data + FMAC_OFF_TGT, FMAC_OFF_CLS - FMAC_OFF_TGT, tgt);
  copy_field(data + FMAC_OFF_CLS, FMAC_OFF_PERM - FMAC_OFF_CLS, cls);
  copy_field(data + FMAC_OFF_PERM, FMAC_OFF_EFFECT - FMAC_OFF_PERM, perm);
  memcpy(data + FMAC_OFF_EFFECT, &effect, sizeof(effect));
  memcpy(data + FMAC_OFF_INVERT, &inv, sizeof(inv));

  return ioc_call(fd, IOC_SEL_ADD_RULE, data, sizeof(data));
}

int ScanDriverFd(void) { return scan_fd_by_link("[fmac_shm]"); }

int ScanCtlFd(void) { return scan_fd_by_link("[fmac_ctl]"); }

static int parse_gki_info(char *out_version, size_t out_size) {
  struct utsname uts;
  const char *release;
  char *endp;
  const char *p;
  const char *tag;
  int major = -1, minor = -1;
  int is_gki = 0;

  if (out_version && out_size > 0)
    out_version[0] = '\0';

  if (uname(&uts) != 0) {
    LOG_ERR("uname failed");
    return -1;
  }

  release = uts.release; /* e.g. "5.10.198-android12-9-g1234567" */

  p = release;
  major = (int)strtol(p, &endp, 10);
  p = endp;
  if (*p == '.') {
    p++;
    minor = (int)strtol(p, &endp, 10);
    p = endp;
  }

  if (major < 0 || minor < 0) {
    LOG_ERR("failed to parse kernel version: %s", release);
    return -1;
  }

  tag = strstr(release, "-android");
  if (tag) {
    const char *q = tag + strlen("-android");
    if (isdigit((unsigned char)*q)) {
      while (isdigit((unsigned char)*q))
        q++;
      if (*q == '-') {
        q++;
        if (isdigit((unsigned char)*q))
          is_gki = 1;
      }
    }
  }

  if (out_version && out_size > 0)
    snprintf(out_version, out_size, "%d.%02d", major, minor);

  LOG_INFO("kernel release=%s parsed_version=%d.%02d is_gki=%d", release, major,
           minor, is_gki);

  return is_gki;
}
} // namespace

static int fd = -1;
static int ctlfd = -1;

/*
 * Lazily acquire the manager-gated control fd: OP_IOCTL is the prctl that makes
 * the kernel attach the "[fmac_ctl]" fd, which we then find in /proc/self/fd.
 * A no-op once acquired.  Returns true when ctlfd is usable.
 */
static bool ensureCtlFd() {
  if (ctlfd >= 0)
    return true;
  if (Ctl(OP_IOCTL) < 0) {
    LOG_ERR("ctl error: operation failed");
    return false;
  }
  const int f = ScanCtlFd();
  if (f < 0) {
    LOG_ERR("fail to scan ctlfd");
    return false;
  }
  ctlfd = f;
  return true;
}

namespace ncore {
static jint ctl(JNIEnv *env, jobject thiz, jint value) {
  (void)env;
  (void)thiz;

  enum Opcode op;
  switch (value) {
  case 1:
    op = OP_AUTHENTICATE;
    break;
  case 3:
    op = OP_IOCTL;
    break;
  default:
    return -1;
  }

  if (Ctl(op) < 0) {
    LOG_ERR("ctl error: operation failed");
  }

  if (value == 1) {
    int f = ScanDriverFd();
    if (f < 0) {
      LOG_ERR("fail to scan fd");
    } else {
      fd = f;
    }
  }

  if (value == 3) {
    int f = ScanCtlFd();
    if (f < 0) {
      LOG_ERR("fail to scan ctlfd");
    } else {
      ctlfd = f;
    }
    LOG_INFO("ctlfd after scan: %d", ctlfd);
  }

  LOG_INFO("ctl fd: %d", fd);
  return (fd < 0) ? -1 : 0;
}

static jint setProfile(JNIEnv *env, jobject thiz, jint uid, jint rootUid,
                       jlong caps, jstring domainStr, jint ns) {
  (void)thiz;

  JniUtfString domain(env, domainStr);
  if (domainStr != nullptr && !domain) {
    return -1;
  }
  if (!ensureCtlFd()) {
    return -1;
  }

  int ret = SetProfile(ctlfd, (int)uid, (int)rootUid, (uint64_t)caps,
                       domain.c_str_or_empty(), (int)ns);
  if (ret < 0) {
    LOG_ERR("setProfile failed");
    return -1;
  }
  return 0;
}

static jint adduid(JNIEnv *env, jobject thiz, jint value) {
  (void)env;
  (void)thiz;

  if (!ensureCtlFd()) {
    return -1;
  }
  if (AddUid(ctlfd, (int)value) < 0) {
    LOG_ERR("adduid failed");
    return -1;
  }
  return 0;
}

static jint deluid(JNIEnv *env, jobject thiz, jint value) {
  (void)env;
  (void)thiz;

  if (!ensureCtlFd()) {
    return -1;
  }
  if (DelUid(ctlfd, (int)value) < 0) {
    LOG_ERR("deluid failed");
    return -1;
  }
  return 0;
}

static jint hasuid(JNIEnv *env, jobject thiz, jint value) {
  (void)env;
  (void)thiz;

  if (!ensureCtlFd()) {
    return -1;
  }
  int has = 0;
  if (HasUid(ctlfd, (int)value, &has) < 0) {
    return -1;
  }
  return has ? 1 : 0;
}

static jint setCap(JNIEnv *env, jobject thiz, jint uid, jlong caps) {
  (void)env;
  (void)thiz;

  if (uid < 0) {
    return -1;
  }
  if (!ensureCtlFd()) {
    return -1;
  }
  if (SetCap(ctlfd, (int)uid, (uint64_t)caps) < 0) {
    LOG_ERR("setCap failed");
    return -1;
  }
  return 0;
}

static jlong getCap(JNIEnv *env, jobject thiz, jint uid) {
  (void)env;
  (void)thiz;

  if (uid < 0) {
    return -1;
  }
  if (!ensureCtlFd()) {
    return -1;
  }
  uint64_t caps = 0;
  if (GetCap(ctlfd, (int)uid, &caps) < 0) {
    LOG_ERR("getCap failed");
    return -1;
  }
  return (jlong)caps;
}

static jint delCap(JNIEnv *env, jobject thiz, jint uid) {
  (void)env;
  (void)thiz;

  if (uid < 0) {
    return -1;
  }
  if (!ensureCtlFd()) {
    return -1;
  }
  if (DelCap(ctlfd, (int)uid) < 0) {
    LOG_ERR("delCap failed");
    return -1;
  }
  return 0;
}

static jint addSelinuxRule(JNIEnv *env, jobject thiz, jstring src, jstring tgt,
                           jstring cls, jstring permStr, jint effect,
                           jboolean invert) {
  (void)thiz;

  JniUtfString srcStr(env, src);
  JniUtfString tgtStr(env, tgt);
  JniUtfString clsStr(env, cls);
  JniUtfString permStrObj(env, permStr);

  if ((src != nullptr && !srcStr) || (tgt != nullptr && !tgtStr) ||
      (cls != nullptr && !clsStr) || (permStr != nullptr && !permStrObj)) {
    return -1;
  }
  if (!ensureCtlFd()) {
    return -1;
  }

  int ret =
      AddSelinuxRule(ctlfd, srcStr.c_str_or_empty(), tgtStr.c_str_or_empty(),
                     clsStr.c_str_or_empty(), permStrObj.c_str_or_empty(),
                     (int)effect, invert ? 1 : 0);

  if (ret < 0) {
    LOG_ERR("addSelinuxRule failed");
    return -1;
  }
  return 0;
}

static jint addRule(JNIEnv *env, jobject thiz, jstring pathStr,
                    jlong statusBits) {
  (void)env;
  (void)thiz;
  (void)pathStr;
  (void)statusBits;
  return 0;
}

static jint delRule(JNIEnv *env, jobject thiz, jstring pathStr) {
  (void)env;
  (void)thiz;
  (void)pathStr;
  return 0;
}

/*
 * Ask the kernel for the current profile table (IOC_GET_PROFILES) and return
 * it as the same text the kernel persists to allow.profile.  The manager parses
 * this instead of keeping its own copy, so the kernel is the single source of
 * truth and no manager-side sync is needed.
 */
static jstring listProfiles(JNIEnv *env, jobject thiz) {
  (void)thiz;

  if (!ensureCtlFd()) {
    LOG_ERR("listProfiles: control fd unavailable");
    return nullptr;
  }

  const uint32_t cap = 64 * 1024;
  // Keep the fmac_ioc header naturally aligned for the reinterpret_cast below.
  std::vector<uint32_t> buffer(
      (sizeof(struct fmac_ioc) + cap + sizeof(uint32_t) - 1) / sizeof(uint32_t),
      0);
  auto *raw = reinterpret_cast<uint8_t *>(buffer.data());
  auto *msg = reinterpret_cast<struct fmac_ioc *>(raw);
  msg->flag = IOC_GET_PROFILES;
  msg->size = cap;

  if (ioctl(ctlfd, IOC_CMD, raw) < 0) {
    LOG_ERR("listProfiles failed: %s", strerror(errno));
    return nullptr;
  }

  // The kernel only writes the text; the zero-filled tail keeps it NUL-ended.
  return env->NewStringUTF(reinterpret_cast<const char *>(msg->data));
}

/*
 * Read a file with root privileges, from a throwaway child process.
 *
 * The rights escalation has side effects that last for the lifetime of the
 * calling task: `setresuid(0, 0, 0)` makes the kernel commit a root cred
 * (uid/gid, capabilities, a fresh `user_struct` with an empty supplementary
 * group list) and switch the task to the profile's SELinux domain while
 * disabling seccomp (see kernel/hook/handle.c and privilege_escalate()).  A
 * `setresuid(saved, ...)` afterwards only puts the uid/gid back; the groups,
 * the `user_struct` and the domain stay changed.
 *
 * Serving the WebUI happens on WebView's shared worker threads, and a later
 * request can be routed to the same thread, so escalating in-process poisons
 * that thread (and with it, e.g., the network request that loads the module's
 * contributor list).  Read in a forked child instead: the child borrows root,
 * pipes the bytes back and exits, and the parent never touches its own
 * credentials.
 *
 * Only async-signal-safe calls are made in the child (setresuid/getuid/open/
 * read/write/close/_exit), so forking a multithreaded process is safe.
 *
 * Used by the module WebUI to serve root-only files without a shell
 * round-trip.  Returns null on failure (missing file, no root, ...).
 */
static jbyteArray readFile(JNIEnv *env, jobject thiz, jstring pathStr) {
  (void)thiz;
  if (pathStr == nullptr)
    return nullptr;

  const char *path = env->GetStringUTFChars(pathStr, nullptr);
  if (path == nullptr)
    return nullptr;

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    env->ReleaseStringUTFChars(pathStr, path);
    return nullptr;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    env->ReleaseStringUTFChars(pathStr, path);
    return nullptr;
  }

  if (pid == 0) {
    /* Child: async-signal-safe calls only, no JNI, no allocation. */
    close(pipefd[0]);

    /*
     * setuid()/setreuid() are not in the app seccomp allowlist (calling them
     * kills the process with SIGSYS); setresuid() is.  The kernel hook
     * escalates this child only, so the parent is untouched.
     */
    if (setresuid(0, 0, 0) != 0 || getuid() != 0)
      _exit(1);

    const int file = open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0)
      _exit(2);

    uint8_t buffer[65536];
    for (;;) {
      const ssize_t n = read(file, buffer, sizeof(buffer));
      if (n > 0) {
        ssize_t off = 0;
        while (off < n) {
          const ssize_t written =
              write(pipefd[1], buffer + off, static_cast<size_t>(n - off));
          if (written > 0) {
            off += written;
          } else if (written < 0 && errno == EINTR) {
            continue;
          } else {
            _exit(3);
          }
        }
      } else if (n == 0) {
        break;
      } else if (errno == EINTR) {
        continue;
      } else {
        _exit(4);
      }
    }

    close(file);
    close(pipefd[1]);
    _exit(0);
  }

  /* Parent: drain the pipe; credentials are never changed here. */
  close(pipefd[1]);

  std::vector<uint8_t> data;
  data.reserve(256 * 1024);
  uint8_t buffer[65536];
  for (;;) {
    const ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
    if (n > 0) {
      data.insert(data.end(), buffer, buffer + n);
    } else if (n == 0) {
      break;
    } else if (errno == EINTR) {
      continue;
    } else {
      data.clear();
      break;
    }
  }
  close(pipefd[0]);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }

  env->ReleaseStringUTFChars(pathStr, path);

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || data.empty())
    return nullptr;

  jbyteArray result = env->NewByteArray(static_cast<jsize>(data.size()));
  if (result != nullptr) {
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(data.size()),
                            reinterpret_cast<const jbyte *>(data.data()));
  }
  return result;
}

/*
 * Stream [srcPath] into [dstPath] with root privileges, from a throwaway child
 * process.  Used to flash an image file to a block device (and the reverse,
 * dumping a partition), without ever holding the whole image in memory.
 *
 * Mirrors readFile(): the child borrows root via setresuid(0, 0, 0) and the
 * kernel hook escalates only that child, so the calling task's credentials
 * (and the shared WebView worker threads) are never touched.  Only
 * async-signal-safe calls are made in the child.  Returns true when the whole
 * source was copied and flushed.
 */
static jboolean writeBlock(JNIEnv *env, jobject thiz, jstring srcStr,
                           jstring dstStr) {
  (void)thiz;
  if (srcStr == nullptr || dstStr == nullptr)
    return JNI_FALSE;

  const char *src = env->GetStringUTFChars(srcStr, nullptr);
  if (src == nullptr)
    return JNI_FALSE;

  const char *dst = env->GetStringUTFChars(dstStr, nullptr);
  if (dst == nullptr) {
    env->ReleaseStringUTFChars(srcStr, src);
    return JNI_FALSE;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    env->ReleaseStringUTFChars(dstStr, dst);
    env->ReleaseStringUTFChars(srcStr, src);
    return JNI_FALSE;
  }

  if (pid == 0) {
    /* Child: async-signal-safe calls only, no JNI, no allocation. */
    if (setresuid(0, 0, 0) != 0 || getuid() != 0)
      _exit(1);

    const int in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0)
      _exit(2);

    const int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) {
      close(in);
      _exit(3);
    }

    /*
     * Flashing an image onto a partition: refuse if the image is larger than
     * the block device, so a wrong partition never gets a truncated write.
     */
    struct stat src_stat;
    struct stat dst_stat;
    if (fstat(in, &src_stat) != 0 || fstat(out, &dst_stat) != 0) {
      close(out);
      close(in);
      _exit(6);
    }
    if (S_ISBLK(dst_stat.st_mode) && S_ISREG(src_stat.st_mode)) {
      const off_t capacity = lseek(out, 0, SEEK_END);
      if (capacity > 0 && src_stat.st_size > capacity) {
        close(out);
        close(in);
        _exit(7);
      }
      if (lseek(out, 0, SEEK_SET) < 0) {
        close(out);
        close(in);
        _exit(8);
      }
    }

    uint8_t buffer[65536];
    for (;;) {
      const ssize_t n = read(in, buffer, sizeof(buffer));
      if (n > 0) {
        ssize_t off = 0;
        while (off < n) {
          const ssize_t written = write(out, buffer + off, n - off);
          if (written > 0) {
            off += written;
          } else if (written < 0 && errno == EINTR) {
            continue;
          } else {
            close(out);
            close(in);
            _exit(4);
          }
        }
      } else if (n == 0) {
        break;
      } else if (errno == EINTR) {
        continue;
      } else {
        close(out);
        close(in);
        _exit(5);
      }
    }

    fsync(out);
    close(out);
    close(in);
    _exit(0);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }

  const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (!ok) {
    LOG_ERR("writeBlock failed: %s -> %s (status=%d)", src, dst,
            WIFEXITED(status) ? WEXITSTATUS(status) : -1);
  }

  env->ReleaseStringUTFChars(dstStr, dst);
  env->ReleaseStringUTFChars(srcStr, src);

  return ok ? JNI_TRUE : JNI_FALSE;
}

/*
 * Locate the vendor_boot partition, with root privileges, from a throwaway
 * child (same escalation as readFile).
 *
 * Candidates are built in the parent - vendor_boot<slot> first, then the
 * slotless vendor_boot when [allowSlotless] is set - so the child only stats
 * them and never allocates.  Returns the first existing path, or null.
 */
static jstring findVendorBoot(JNIEnv *env, jobject thiz, jstring slotStr,
                              jboolean allowSlotless) {
  (void)thiz;
  JniUtfString slot(env, slotStr);
  const char *slot_c = slot ? slot.c_str() : "";

  static const char *bases[] = {"/dev/block/by-name",
                                "/dev/block/bootdevice/by-name"};

  char candidates[8][256];
  size_t lengths[8];
  int count = 0;
  for (const char *base : bases) {
    int n = snprintf(candidates[count], sizeof(candidates[count]),
                     "%s/vendor_boot%s", base, slot_c);
    if (n > 0 && n < static_cast<int>(sizeof(candidates[count]))) {
      lengths[count] = static_cast<size_t>(n);
      ++count;
    }
    if (allowSlotless) {
      n = snprintf(candidates[count], sizeof(candidates[count]), "%s/vendor_boot",
                   base);
      if (n > 0 && n < static_cast<int>(sizeof(candidates[count]))) {
        lengths[count] = static_cast<size_t>(n);
        ++count;
      }
    }
  }
  if (count == 0)
    return nullptr;

  int pipefd[2];
  if (pipe(pipefd) != 0)
    return nullptr;

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return nullptr;
  }

  if (pid == 0) {
    /* Child: async-signal-safe calls only, no JNI, no allocation. */
    close(pipefd[0]);
    if (setresuid(0, 0, 0) != 0 || getuid() != 0)
      _exit(1);

    for (int i = 0; i < count; ++i) {
      if (access(candidates[i], F_OK) != 0)
        continue;
      size_t off = 0;
      while (off < lengths[i]) {
        const ssize_t written =
            write(pipefd[1], candidates[i] + off, lengths[i] - off);
        if (written > 0) {
          off += static_cast<size_t>(written);
        } else if (written < 0 && errno == EINTR) {
          continue;
        } else {
          _exit(2);
        }
      }
      close(pipefd[1]);
      _exit(0);
    }
    _exit(3);
  }

  close(pipefd[1]);

  char buffer[256];
  ssize_t total = 0;
  for (;;) {
    const ssize_t n =
        read(pipefd[0], buffer + total, sizeof(buffer) - 1 - total);
    if (n > 0) {
      total += n;
      if (total >= static_cast<ssize_t>(sizeof(buffer)) - 1)
        break;
    } else if (n == 0) {
      break;
    } else if (errno == EINTR) {
      continue;
    } else {
      total = 0;
      break;
    }
  }
  close(pipefd[0]);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }

  if (total <= 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    return nullptr;

  buffer[total] = '\0';
  return env->NewStringUTF(buffer);
}

static void helloLog(JNIEnv *env, jobject thiz) {
  (void)env;
  (void)thiz;
  LOG_DEBUG("Hello, this is a log from C!");
  LOG_INFO("ncore build-as lib (C version)");
}

static jboolean isGki(JNIEnv *env, jobject thiz) {
  (void)env;
  (void)thiz;
  int ret = parse_gki_info(NULL, 0);
  return (ret == 1) ? JNI_TRUE : JNI_FALSE;
}

static jstring kernelVersion(JNIEnv *env, jobject thiz) {
  (void)thiz;
  char ver[16];
  int ret = parse_gki_info(ver, sizeof(ver));
  if (ret < 0) {
    return NULL;
  }
  return env->NewStringUTF(ver);
}

/*
 * The raw kernel release from uname(), e.g. "5.15.194-android13-8-...".
 * The manager parses the "-androidNN" tag out of it to build the KMI name;
 * kernelVersion() only returns the parsed major.minor.
 */
static jstring kernelRelease(JNIEnv *env, jobject thiz) {
  (void)thiz;
  struct utsname uts;
  if (uname(&uts) != 0) {
    LOG_ERR("uname failed");
    return NULL;
  }
  return env->NewStringUTF(uts.release);
}

/*
 * The running kernel module's version (IOC_GET_VERSION).  The manager compares
 * it with the version value it was built with, so it can tell that the
 * flashed LKM is stale and never install a userspace ncore that does not match.
 */
static jstring moduleVersion(JNIEnv *env, jobject thiz) {
  (void)thiz;

  if (!ensureCtlFd()) {
    LOG_ERR("moduleVersion: control fd unavailable");
    return nullptr;
  }

  char buf[64];
  memset(buf, 0, sizeof(buf));
  if (ioc_call(ctlfd, IOC_GET_VERSION, buf, sizeof(buf)) < 0) {
    LOG_ERR("moduleVersion failed: %s", strerror(errno));
    return nullptr;
  }
  buf[sizeof(buf) - 1] = '\0';
  return env->NewStringUTF(buf);
}

/*
 * Kernel feature toggles (IOC_FEATURE_*).  featureList() returns the
 * "<id> <name> <value>" text the kernel renders, so the manager can show which
 * features exist and whether they are on; featureGet/featureSet act on one.
 * The wire layout is a uint32 id followed by a uint64 value, matching
 * FMAC_OFF_FEATURE_VALUE.
 */
static jstring featureList(JNIEnv *env, jobject thiz) {
  (void)thiz;

  if (!ensureCtlFd()) {
    LOG_ERR("featureList: control fd unavailable");
    return nullptr;
  }

  char buf[1024];
  memset(buf, 0, sizeof(buf));
  if (ioc_call(ctlfd, IOC_FEATURE_LIST, buf, sizeof(buf)) < 0) {
    LOG_ERR("featureList failed: %s", strerror(errno));
    return nullptr;
  }
  buf[sizeof(buf) - 1] = '\0';
  return env->NewStringUTF(buf);
}

static jlong featureGet(JNIEnv *env, jobject thiz, jint id) {
  (void)env;
  (void)thiz;

  if (!ensureCtlFd())
    return -1;

  uint8_t payload[FMAC_DATA_FEATURE];
  uint32_t fid = (uint32_t)id;
  uint64_t value = 0;
  memcpy(payload, &fid, sizeof(fid));
  memcpy(payload + FMAC_OFF_FEATURE_VALUE, &value, sizeof(value));

  if (ioc_call(ctlfd, IOC_FEATURE_GET, payload, sizeof(payload)) < 0) {
    LOG_ERR("featureGet(%d) failed: %s", id, strerror(errno));
    return -1;
  }
  memcpy(&value, payload + FMAC_OFF_FEATURE_VALUE, sizeof(value));
  return (jlong)value;
}

static jint featureSet(JNIEnv *env, jobject thiz, jint id, jlong value) {
  (void)env;
  (void)thiz;

  if (!ensureCtlFd())
    return -1;

  uint8_t payload[FMAC_DATA_FEATURE];
  uint32_t fid = (uint32_t)id;
  uint64_t fvalue = (uint64_t)value;
  memcpy(payload, &fid, sizeof(fid));
  memcpy(payload + FMAC_OFF_FEATURE_VALUE, &fvalue, sizeof(fvalue));

  if (ioc_call(ctlfd, IOC_FEATURE_SET, payload, sizeof(payload)) < 0) {
    LOG_ERR("featureSet(%d, %lld) failed: %s", id, (long long)value,
            strerror(errno));
    return -1;
  }
  return 0;
}

static jstring getSlotSuffix(JNIEnv *env, jobject) {
  char value[PROP_VALUE_MAX];

  if (__system_property_get("ro.boot.slot_suffix", value) > 0 &&
      value[0] != '\0') {
    return env->NewStringUTF(value);
  }

  char slot[PROP_VALUE_MAX];
  if (__system_property_get("ro.boot.slot", slot) > 0 && slot[0] != '\0') {
    snprintf(value, sizeof(value), "_%s", slot);
    return env->NewStringUTF(value);
  }
  return nullptr;
}
} // namespace ncore

const JNINativeMethod gMethods[] = {
    {"ctl", "(I)I", (void *)ncore::ctl},
    {"setProfile", "(IIJLjava/lang/String;I)I", (void *)ncore::setProfile},
    {"adduid", "(I)I", (void *)ncore::adduid},
    {"deluid", "(I)I", (void *)ncore::deluid},
    {"hasuid", "(I)I", (void *)ncore::hasuid},
    {"setCap", "(IJ)I", (void *)ncore::setCap},
    {"getCap", "(I)J", (void *)ncore::getCap},
    {"delCap", "(I)I", (void *)ncore::delCap},
    {"addSelinuxRule",
     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/"
     "String;IZ)I",
     (void *)ncore::addSelinuxRule},
    {"addRule", "(Ljava/lang/String;J)I", (void *)ncore::addRule},
    {"delRule", "(Ljava/lang/String;)I", (void *)ncore::delRule},
    {"listProfiles", "()Ljava/lang/String;", (void *)ncore::listProfiles},
    {"readFile", "(Ljava/lang/String;)[B", (void *)ncore::readFile},
    {"writeBlock", "(Ljava/lang/String;Ljava/lang/String;)Z",
     (void *)ncore::writeBlock},
    {"helloLog", "()V", (void *)ncore::helloLog},
    {"isGki", "()Z", (void *)ncore::isGki},
    {"kernelVersion", "()Ljava/lang/String;", (void *)ncore::kernelVersion},
    {"kernelRelease", "()Ljava/lang/String;", (void *)ncore::kernelRelease},
    {"moduleVersion", "()Ljava/lang/String;", (void *)ncore::moduleVersion},
    {"featureList", "()Ljava/lang/String;", (void *)ncore::featureList},
    {"featureGet", "(I)J", (void *)ncore::featureGet},
    {"featureSet", "(IJ)I", (void *)ncore::featureSet},
    {"getSlotSuffix", "()Ljava/lang/String;", (void *)ncore::getSlotSuffix},
    {"findVendorBoot", "(Ljava/lang/String;Z)Ljava/lang/String;",
     (void *)ncore::findVendorBoot},
};

static int registerNativeMethods(JNIEnv *env) {
  jclass clazz = env->FindClass("me/nekosu/aqnya/core/Ncore");
  if (clazz == NULL) {
    LOG_ERR("FindClass failed");
    return -1;
  }

  if (env->RegisterNatives(clazz, gMethods,
                           sizeof(gMethods) / sizeof(gMethods[0])) < 0) {
    LOG_ERR("RegisterNatives failed");
    return -1;
  }

  return 0;
}

bool authenticate(void) { return Ctl(OP_AUTHENTICATE) >= 0; }

inline JNIEnv *GetJNIEnv(JavaVM *vm) {
  JNIEnv *env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
    LOG_ERR("GetEnv failed");
    return nullptr;
  }
  return env;
}

} // namespace jni

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  (void)reserved;

  JNIEnv *env = jni::GetJNIEnv(vm);
  if (!env)
    return -1;

  if (!jni::authenticate()) {
    LOG_ERR("ctl error: authenticate failed");
  }

  if (jni::registerNativeMethods(env) < 0) {
    LOG_ERR("registerNativeMethods failed");
    return -1;
  }

  return JNI_VERSION_1_6;
}