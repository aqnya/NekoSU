// SPDX-License-Identifier: GPL-3.0-or-later
//
// See prop_internal.hpp.  Layout and ordering follow AOSP bionic
// (libc/system_properties) and system/core (property_info parser, persistent
// properties).
#include "prop/prop_internal.hpp"

#include <fcntl.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <functional>

namespace prop {
namespace {

// ── on-disk layout (bionic system_properties) ───────────────────────────────

constexpr uint32_t kAreaMagic = 0x504f5250;
constexpr uint32_t kAreaVersion = 0xfc6ed0ab;
constexpr size_t kValueMax = 92;
constexpr uint32_t kLongFlag = 1u << 16;
constexpr size_t kLongErrBufSize = 56;
constexpr const char kLongErr[] = "Must use __system_property_read_callback() to read";
constexpr const char *kDefaultContext = "u:object_r:default_prop:s0";
constexpr const char *kPreSplitContext = "u:object_r:properties_device:s0";
constexpr const char *kDir = "/dev/__properties__";

struct AreaHeader {
  uint32_t bytes_used;
  uint32_t serial;
  uint32_t magic;
  uint32_t version;
  uint32_t reserved[28];
  char data[0];
};

struct TrieNode {
  uint32_t namelen;
  uint32_t prop;
  uint32_t left;
  uint32_t right;
  uint32_t children;
  char name[0];
};

struct PropInfo {
  uint32_t serial;
  union {
    char value[kValueMax];
    struct {
      char error_message[kLongErrBufSize];
      uint32_t offset;
    } long_property;
  };
  char name[0];
};

static_assert(sizeof(AreaHeader) == 128, "prop_area header size");
static_assert(sizeof(TrieNode) == 20, "prop_trie_node size");
static_assert(sizeof(PropInfo) == 96, "prop_info size");

inline uint32_t align4(uint32_t v) { return (v + 3u) & ~3u; }

inline uint32_t load(const uint32_t *p, int order) {
  return __atomic_load_n(const_cast<uint32_t *>(p), order);
}
inline void store(uint32_t *p, uint32_t v, int order) { __atomic_store_n(p, v, order); }

void futex_wake(uint32_t *addr) {
  syscall(SYS_futex, addr, FUTEX_WAKE, INT32_MAX, nullptr, nullptr, 0);
}
int futex_wait(uint32_t *addr, uint32_t val, const struct timespec *to) {
  return syscall(SYS_futex, addr, FUTEX_WAIT, val, to, nullptr, 0);
}

uint32_t read_u32(const std::vector<uint8_t> &b, size_t off) {
  if (off + 4 > b.size()) return ~0u;
  uint32_t v;
  memcpy(&v, b.data() + off, 4);
  return v;
}
const char *read_cstr(const std::vector<uint8_t> &b, size_t off) {
  if (off >= b.size()) return nullptr;
  return reinterpret_cast<const char *>(b.data() + off);
}

// Binary search helper: f(i) returns strcmp-like ordering.
template <typename F>
int bsearch_index(uint32_t n, F &&f) {
  int lo = 0, hi = static_cast<int>(n) - 1;
  while (hi >= lo) {
    int mid = (lo + hi) / 2;
    int cmp = f(mid);
    if (cmp == 0) return mid;
    if (cmp < 0) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

} // namespace

// ── property_info (context trie) parser ─────────────────────────────────────
//
// Layout is system/core/property_service/libpropertyinfoparser.

namespace {

uint32_t info_num_contexts(const std::vector<uint8_t> &d) {
  uint32_t ctx = read_u32(d, 12);
  return read_u32(d, ctx);
}
const char *info_context(const std::vector<uint8_t> &d, uint32_t index) {
  uint32_t ctx = read_u32(d, 12);
  uint32_t str = read_u32(d, ctx + 4 + index * 4);
  return read_cstr(d, str);
}

struct Node {
  const std::vector<uint8_t> *d;
  size_t off;
  uint32_t pe() const { return read_u32(*d, off); }
  uint32_t ctx_index() const { return read_u32(*d, pe() + 8); }
  uint32_t num_children() const { return read_u32(*d, off + 4); }
  size_t child(size_t i) const {
    uint32_t arr = read_u32(*d, off + 8);
    return read_u32(*d, arr + i * 4);
  }
  const char *child_name(size_t child_off) const {
    uint32_t pe_off = read_u32(*d, child_off);
    return read_cstr(*d, read_u32(*d, pe_off));
  }
  uint32_t num_prefixes() const { return read_u32(*d, off + 12); }
  uint32_t prefix_ctx(size_t i) const {
    uint32_t arr = read_u32(*d, off + 16);
    uint32_t pe = read_u32(*d, arr + i * 4);
    return read_u32(*d, pe + 8);
  }
  const char *prefix_name(size_t i, uint32_t *len) const {
    uint32_t arr = read_u32(*d, off + 16);
    uint32_t pe = read_u32(*d, arr + i * 4);
    *len = read_u32(*d, pe + 4);
    return read_cstr(*d, read_u32(*d, pe));
  }
  uint32_t num_exact() const { return read_u32(*d, off + 20); }
  uint32_t exact_ctx(size_t i) const {
    uint32_t arr = read_u32(*d, off + 24);
    uint32_t pe = read_u32(*d, arr + i * 4);
    return read_u32(*d, pe + 8);
  }
  const char *exact_name(size_t i) const {
    uint32_t arr = read_u32(*d, off + 24);
    uint32_t pe = read_u32(*d, arr + i * 4);
    return read_cstr(*d, read_u32(*d, pe));
  }
  bool find_child(const char *name, uint32_t namelen, size_t *out) const {
    uint32_t n = num_children();
    int idx = bsearch_index(n, [&](int i) {
      const char *cn = child_name(child(i));
      int cmp = strncmp(cn, name, namelen);
      if (cmp == 0 && cn[namelen] != '\0') return 1;
      return cmp;
    });
    if (idx < 0) return false;
    *out = child(static_cast<size_t>(idx));
    return true;
  }
  void check_prefix(const char *remaining, uint32_t *ctx) const {
    uint32_t rem = strlen(remaining);
    for (uint32_t i = 0; i < num_prefixes(); i++) {
      uint32_t plen = 0;
      const char *pname = prefix_name(i, &plen);
      if (!pname || plen > rem) continue;
      if (strncmp(pname, remaining, plen) == 0) {
        uint32_t c = prefix_ctx(i);
        if (c != ~0u) *ctx = c;
        return;
      }
    }
  }
};

uint32_t info_context_index(const std::vector<uint8_t> &d, const char *name) {
  uint32_t ret_ctx = ~0u;
  const char *remaining = name;
  Node node{&d, read_u32(d, 20)};
  for (;;) {
    const char *sep = strchr(remaining, '.');
    if (node.ctx_index() != ~0u) ret_ctx = node.ctx_index();
    node.check_prefix(remaining, &ret_ctx);
    if (!sep) break;
    uint32_t sublen = static_cast<uint32_t>(sep - remaining);
    size_t child;
    if (!node.find_child(remaining, sublen, &child)) break;
    node.off = child;
    remaining = sep + 1;
  }
  for (uint32_t i = 0; i < node.num_exact(); i++) {
    const char *en = node.exact_name(i);
    if (en && strcmp(en, remaining) == 0) {
      uint32_t c = node.exact_ctx(i);
      if (c != ~0u) ret_ctx = c;
      return ret_ctx;
    }
  }
  node.check_prefix(remaining, &ret_ctx);
  return ret_ctx;
}

} // namespace

// ── prop area ───────────────────────────────────────────────────────────────

struct SystemProperties::Area {
  void *base = nullptr;
  size_t size = 0;
  size_t data_size = 0;
  std::string context;

  AreaHeader *hdr() { return reinterpret_cast<AreaHeader *>(base); }
  char *data() { return reinterpret_cast<char *>(base) + sizeof(AreaHeader); }
  char *dirty_backup() { return data() + sizeof(TrieNode); }

  void *to_obj(uint32_t off) {
    if (off > data_size) return nullptr;
    return data() + off;
  }

  static Area *map_rw(const std::string &path, const std::string &context) {
    int fd = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return nullptr;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < static_cast<off_t>(sizeof(AreaHeader))) {
      close(fd);
      return nullptr;
    }
    void *m = mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return nullptr;
    auto *h = reinterpret_cast<AreaHeader *>(m);
    if (h->magic != kAreaMagic || h->version != kAreaVersion) {
      munmap(m, st.st_size);
      return nullptr;
    }
    auto *a = new Area();
    a->base = m;
    a->size = st.st_size;
    a->data_size = st.st_size - sizeof(AreaHeader);
    a->context = context;
    return a;
  }

  // Fresh area in anonymous memory, initialised like bionic's prop_area ctor.
  static Area *create_anon(size_t size) {
    void *m = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) return nullptr;
    auto *h = reinterpret_cast<AreaHeader *>(m);
    h->bytes_used = sizeof(TrieNode) + align4(kValueMax);
    h->serial = 0;
    h->magic = kAreaMagic;
    h->version = kAreaVersion;
    memset(h->reserved, 0, sizeof(h->reserved));
    auto *a = new Area();
    a->base = m;
    a->size = size;
    a->data_size = size - sizeof(AreaHeader);
    return a;
  }

  void collect(std::vector<Property> *out) {
    auto fn = [](const PropInfo *pi, void *cookie) {
      auto *v = static_cast<std::vector<Property> *>(cookie);
      uint32_t serial = load(&pi->serial, __ATOMIC_RELAXED);
      const char *value = (serial & kLongFlag)
                              ? reinterpret_cast<const char *>(pi) + pi->long_property.offset
                              : pi->value;
      v->push_back(Property{pi->name, value});
    };
    foreach (fn, out);
  }

  void *allocate(size_t sz, uint32_t *off) {
    uint32_t aligned = align4(static_cast<uint32_t>(sz));
    uint32_t used = __atomic_load_n(&hdr()->bytes_used, __ATOMIC_RELAXED);
    if (used + aligned > data_size) return nullptr;
    *off = used;
    __atomic_store_n(&hdr()->bytes_used, used + aligned, __ATOMIC_RELAXED);
    return data() + *off;
  }

  TrieNode *new_node(const char *name, uint32_t namelen, uint32_t *off) {
    void *p = allocate(sizeof(TrieNode) + namelen + 1, off);
    if (!p) return nullptr;
    auto *n = reinterpret_cast<TrieNode *>(p);
    n->namelen = namelen;
    __atomic_store_n(&n->prop, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&n->left, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&n->right, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&n->children, 0u, __ATOMIC_RELAXED);
    memcpy(n->name, name, namelen);
    n->name[namelen] = '\0';
    return n;
  }

  PropInfo *new_info(const char *name, uint32_t namelen, const char *value,
                     uint32_t valuelen, uint32_t *off) {
    void *p = allocate(sizeof(PropInfo) + namelen + 1, off);
    if (!p) return nullptr;
    auto *info = reinterpret_cast<PropInfo *>(p);
    if (valuelen >= kValueMax) {
      uint32_t lv_off;
      char *lv = reinterpret_cast<char *>(allocate(valuelen + 1, &lv_off));
      if (!lv) return nullptr;
      memcpy(lv, value, valuelen);
      lv[valuelen] = '\0';
      lv_off -= *off;  // relative to prop_info
      memcpy(info->long_property.error_message, kLongErr, sizeof(kLongErr) - 1);
      info->long_property.error_message[sizeof(kLongErr) - 1] = '\0';
      info->long_property.offset = lv_off;
      uint32_t serial = (static_cast<uint32_t>(sizeof(kLongErr) - 1) << 24) | kLongFlag;
      __atomic_store_n(&info->serial, serial, __ATOMIC_RELAXED);
    } else {
      memcpy(info->value, value, valuelen);
      info->value[valuelen] = '\0';
      uint32_t serial = valuelen << 24;
      __atomic_store_n(&info->serial, serial, __ATOMIC_RELAXED);
    }
    memcpy(info->name, name, namelen);
    info->name[namelen] = '\0';
    return info;
  }

  static int cmp_name(const char *one, uint32_t one_len, const char *two, uint32_t two_len) {
    if (one_len < two_len) return -1;
    if (one_len > two_len) return 1;
    return strncmp(one, two, one_len);
  }

  TrieNode *find_node(TrieNode *trie, const char *name, uint32_t namelen, bool alloc) {
    TrieNode *cur = trie;
    while (cur) {
      int ret = cmp_name(name, namelen, cur->name, cur->namelen);
      if (ret == 0) return cur;
      uint32_t *field = ret < 0 ? &cur->left : &cur->right;
      uint32_t off = __atomic_load_n(field, __ATOMIC_RELAXED);
      if (off != 0) {
        cur = reinterpret_cast<TrieNode *>(to_obj(off));
        continue;
      }
      if (!alloc) return nullptr;
      uint32_t new_off;
      TrieNode *nn = new_node(name, namelen, &new_off);
      if (nn) store(field, new_off, __ATOMIC_RELEASE);
      return nn;
    }
    return nullptr;
  }

  PropInfo *find_prop(TrieNode *trie, const char *name, uint32_t namelen, const char *value,
                      uint32_t valuelen, bool alloc) {
    if (!trie) return nullptr;
    const char *remaining = name;
    TrieNode *cur = trie;
    for (;;) {
      const char *sep = strchr(remaining, '.');
      bool want_subtree = sep != nullptr;
      uint32_t sublen = want_subtree ? static_cast<uint32_t>(sep - remaining)
                                     : static_cast<uint32_t>(strlen(remaining));
      if (sublen == 0) return nullptr;
      uint32_t children_off = __atomic_load_n(&cur->children, __ATOMIC_RELAXED);
      TrieNode *root;
      if (children_off != 0) {
        root = reinterpret_cast<TrieNode *>(to_obj(children_off));
      } else if (alloc) {
        uint32_t new_off;
        root = new_node(remaining, sublen, &new_off);
        if (root) store(&cur->children, new_off, __ATOMIC_RELEASE);
      } else {
        root = nullptr;
      }
      if (!root) return nullptr;
      cur = find_node(root, remaining, sublen, alloc);
      if (!cur) return nullptr;
      if (!want_subtree) break;
      remaining = sep + 1;
    }
    uint32_t prop_off = __atomic_load_n(&cur->prop, __ATOMIC_RELAXED);
    if (prop_off != 0) return reinterpret_cast<PropInfo *>(to_obj(prop_off));
    if (!alloc) return nullptr;
    uint32_t new_off;
    PropInfo *info = new_info(name, namelen, value, valuelen, &new_off);
    if (info) store(&cur->prop, new_off, __ATOMIC_RELEASE);
    return info;
  }

  PropInfo *find(const char *name) {
    return find_prop(reinterpret_cast<TrieNode *>(to_obj(0)), name, strlen(name), nullptr, 0,
                     false);
  }

  TrieNode *find_leaf(const char *name) {
    TrieNode *cur = reinterpret_cast<TrieNode *>(to_obj(0));
    const char *remaining = name;
    if (!cur) return nullptr;
    for (;;) {
      const char *sep = strchr(remaining, '.');
      bool want_subtree = sep != nullptr;
      uint32_t sublen = want_subtree ? static_cast<uint32_t>(sep - remaining)
                                     : static_cast<uint32_t>(strlen(remaining));
      if (sublen == 0) return nullptr;
      uint32_t children_off = __atomic_load_n(&cur->children, __ATOMIC_RELAXED);
      if (children_off == 0) return nullptr;
      TrieNode *root = reinterpret_cast<TrieNode *>(to_obj(children_off));
      cur = find_node(root, remaining, sublen, false);
      if (!cur) return nullptr;
      if (!want_subtree) return cur;
      remaining = sep + 1;
    }
  }

  bool add(const char *name, uint32_t namelen, const char *value, uint32_t valuelen) {
    return find_prop(reinterpret_cast<TrieNode *>(to_obj(0)), name, namelen, value, valuelen,
                     true) != nullptr;
  }

  int update(PropInfo *pi, const char *value, uint32_t len) {
    if (len >= kValueMax) return -1;
    uint32_t serial = __atomic_load_n(&pi->serial, __ATOMIC_RELAXED);
    uint32_t old_len = serial >> 24;
    if (old_len + 1 > kValueMax) old_len = kValueMax - 1;
    memcpy(dirty_backup(), pi->value, old_len + 1);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    serial |= 1;
    __atomic_store_n(&pi->serial, serial, __ATOMIC_RELAXED);
    memcpy(pi->value, value, len);
    pi->value[len] = '\0';
    __atomic_thread_fence(__ATOMIC_RELEASE);
    uint32_t visible = (len << 24) | ((serial + 1) & 0x00ffffff);
    __atomic_store_n(&pi->serial, visible, __ATOMIC_RELAXED);
    futex_wake(&pi->serial);
    // Restore the counter so a changed ro.* serial is not a giveaway.
    __atomic_thread_fence(__ATOMIC_RELEASE);
    uint32_t hidden = (len << 24) | ((serial & ~1u) & 0x00ffffff);
    __atomic_store_n(&pi->serial, hidden, __ATOMIC_RELAXED);
    return 0;
  }

  bool remove(const char *name) {
    TrieNode *leaf = find_leaf(name);
    if (!leaf) return false;
    uint32_t prop_off = __atomic_load_n(&leaf->prop, __ATOMIC_RELAXED);
    if (prop_off == 0) return false;
    store(&leaf->prop, 0u, __ATOMIC_RELEASE);
    auto *pi = reinterpret_cast<PropInfo *>(to_obj(prop_off));
    if (!pi) return true;
    uint32_t serial = __atomic_load_n(&pi->serial, __ATOMIC_RELAXED);
    if (serial & kLongFlag) {
      char *lv = reinterpret_cast<char *>(pi) + pi->long_property.offset;
      memset(lv, 0, strlen(lv));
    }
    memset(pi, 0, sizeof(PropInfo));
    return true;
  }

  bool foreach (void (*fn)(const PropInfo *, void *), void *cookie) {
    return foreach_node(reinterpret_cast<TrieNode *>(to_obj(0)), fn, cookie);
  }  bool foreach_node(TrieNode *t, void (*fn)(const PropInfo *, void *), void *cookie) {
    if (!t) return false;
    uint32_t l = __atomic_load_n(&t->left, __ATOMIC_RELAXED);
    if (l && !foreach_node(reinterpret_cast<TrieNode *>(to_obj(l)), fn, cookie)) return false;
    uint32_t p = __atomic_load_n(&t->prop, __ATOMIC_RELAXED);
    if (p) {
      auto *pi = reinterpret_cast<PropInfo *>(to_obj(p));
      if (pi) fn(pi, cookie);
    }
    uint32_t c = __atomic_load_n(&t->children, __ATOMIC_RELAXED);
    if (c && !foreach_node(reinterpret_cast<TrieNode *>(to_obj(c)), fn, cookie)) return false;
    uint32_t r = __atomic_load_n(&t->right, __ATOMIC_RELAXED);
    if (r && !foreach_node(reinterpret_cast<TrieNode *>(to_obj(r)), fn, cookie)) return false;
    return true;
  }
};

// ── SystemProperties ────────────────────────────────────────────────────────

SystemProperties &SystemProperties::Instance() {
  static SystemProperties inst;
  return inst;
}

bool SystemProperties::MapSerial() {
  serial_area_ = Area::map_rw(props_dir_ + "/properties_serial", "properties_serial");
  return serial_area_ != nullptr;
}

bool SystemProperties::Init() {
  if (initialized_) return true;

  struct stat st;
  if (stat(kDir, &st) != 0) {
    // Legacy PreSplit layout: a single prop-area file.
    if (stat("/dev/__properties__", &st) == 0 && !S_ISDIR(st.st_mode)) {
      presplit_ = true;
      props_dir_ = "/dev/__properties__";
      presplit_area_ = Area::map_rw(props_dir_, kPreSplitContext);
      serial_area_ = presplit_area_;
      initialized_ = presplit_area_ != nullptr;
      return initialized_;
    }
    return false;
  }
  props_dir_ = kDir;

  // Serialized layout: parse property_info.
  std::string info_path = props_dir_ + "/property_info";
  int fd = open(info_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  struct stat ist;
  if (fstat(fd, &ist) != 0 || ist.st_size < 24) {
    close(fd);
    return false;
  }
  info_.resize(ist.st_size);
  ssize_t n = read(fd, info_.data(), info_.size());
  close(fd);
  if (n != static_cast<ssize_t>(info_.size())) return false;
  serialized_ = true;

  if (!MapSerial()) return false;
  areas_.assign(info_num_contexts(info_), nullptr);
  initialized_ = true;
  return true;
}

SystemProperties::Area *SystemProperties::AreaForIndex(uint32_t index) {
  if (presplit_) return presplit_area_;
  if (index >= areas_.size()) return nullptr;
  if (areas_[index]) return areas_[index];
  const char *ctx = info_context(info_, index);
  if (!ctx) return nullptr;
  areas_[index] = Area::map_rw(props_dir_ + "/" + ctx, ctx);
  return areas_[index];
}

SystemProperties::Area *SystemProperties::AreaForName(const std::string &name) {
  if (!initialized_) return nullptr;
  if (presplit_) return presplit_area_;
  uint32_t idx = info_context_index(info_, name.c_str());
  if (idx == ~0u) return nullptr;
  return AreaForIndex(idx);
}

std::string SystemProperties::GetContext(const std::string &name) {
  if (presplit_) return kPreSplitContext;
  uint32_t idx = info_context_index(info_, name.c_str());
  if (idx == ~0u) return kDefaultContext;
  const char *ctx = info_context(info_, idx);
  return ctx ? ctx : kDefaultContext;
}

std::vector<std::string> SystemProperties::Contexts() {
  std::vector<std::string> out;
  if (presplit_) {
    out.emplace_back(kPreSplitContext);
    return out;
  }
  uint32_t n = info_num_contexts(info_);
  for (uint32_t i = 0; i < n; i++) {
    const char *ctx = info_context(info_, i);
    if (ctx) out.emplace_back(ctx);
  }
  return out;
}

std::optional<std::string> SystemProperties::Get(const std::string &name) {
  if (!initialized_) return std::nullopt;
  Area *pa = AreaForName(name);
  if (!pa) return std::nullopt;
  PropInfo *pi = pa->find(name.c_str());
  if (!pi) return std::nullopt;

  uint32_t serial = load(&pi->serial, __ATOMIC_RELAXED);
  if (serial & kLongFlag) {
    // Long value lives after the prop_info; it is immutable (ro.* only).
    return std::string(reinterpret_cast<const char *>(pi) + pi->long_property.offset);
  }

  char buf[kValueMax];
  for (;;) {
    uint32_t len = serial >> 24;
    if (len >= kValueMax) len = kValueMax - 1;
    if (serial & 1) {
      memcpy(buf, pa->dirty_backup(), len + 1);
    } else {
      memcpy(buf, pi->value, len + 1);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    uint32_t again = load(&pi->serial, __ATOMIC_RELAXED);
    if (again == serial) break;
    serial = again;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
  }
  return std::string(buf);
}

int SystemProperties::Set(const std::string &name, const std::string &value) {
  if (!initialized_) return -ENODEV;
  Area *pa = AreaForName(name);
  if (!pa) return -ENOENT;
  bool can_be_long = strncmp(name.c_str(), "ro.", 3) == 0;
  if (value.size() >= kValueMax && !can_be_long) return -EINVAL;

  PropInfo *pi = pa->find(name.c_str());
  bool old_long = pi && (load(&pi->serial, __ATOMIC_RELAXED) & kLongFlag);
  if (pi && !old_long && value.size() < kValueMax) {
    int ret = pa->update(pi, value.c_str(), static_cast<uint32_t>(value.size()));
    if (ret != 0) return ret;
  } else {
    // New property, or an inline/long layout change: delete then add.
    if (pi) pa->remove(name.c_str());
    if (!pa->add(name.c_str(), static_cast<uint32_t>(name.size()), value.c_str(),
                 static_cast<uint32_t>(value.size())))
      return -ENOMEM;
  }
  if (serial_area_) {
    uint32_t *s = &serial_area_->hdr()->serial;
    store(s, load(s, __ATOMIC_RELAXED) + 1, __ATOMIC_RELEASE);
    futex_wake(s);
  }
  return 0;
}

bool SystemProperties::Delete(const std::string &name) {
  if (!initialized_) return false;
  Area *pa = AreaForName(name);
  if (!pa) return false;
  bool ok = pa->remove(name.c_str());
  if (ok && serial_area_) {
    uint32_t *s = &serial_area_->hdr()->serial;
    store(s, load(s, __ATOMIC_RELAXED) + 1, __ATOMIC_RELEASE);
    futex_wake(s);
  }
  return ok;
}

void SystemProperties::ForEach(const std::function<void(const Property &)> &fn) {
  if (!initialized_) return;
  auto visitor = [](const PropInfo *pi, void *cookie) {
    auto *cb = static_cast<const std::function<void(const Property &)> *>(cookie);
    const char *value;
    uint32_t serial = load(&pi->serial, __ATOMIC_RELAXED);
    if (serial & kLongFlag) {
      value = reinterpret_cast<const char *>(pi) + pi->long_property.offset;
    } else {
      value = pi->value;
    }
    (*cb)(Property{pi->name, value});
  };
  if (presplit_) {
    if (presplit_area_) presplit_area_->foreach (visitor, const_cast<void *>(static_cast<const void *>(&fn)));
    return;
  }
  for (uint32_t i = 0; i < areas_.size(); i++) {
    Area *pa = AreaForIndex(i);
    if (pa) pa->foreach (visitor, const_cast<void *>(static_cast<const void *>(&fn)));
  }
}

bool SystemProperties::Wait(const std::string &name, const std::optional<std::string> &old_value,
                            double timeout_sec) {
  if (!initialized_) return false;
  struct timespec ts;
  struct timespec *tsp = nullptr;
  if (timeout_sec >= 0) {
    ts.tv_sec = static_cast<time_t>(timeout_sec);
    ts.tv_nsec = static_cast<long>((timeout_sec - ts.tv_sec) * 1e9);
    tsp = &ts;
  }
  for (;;) {
    Area *pa = AreaForName(name);
    PropInfo *pi = pa ? pa->find(name.c_str()) : nullptr;
    if (pi) {
      auto cur = Get(name);
      if (!old_value.has_value()) return true;
      if (cur.has_value() && *cur != *old_value) return true;
      uint32_t serial = load(&pi->serial, __ATOMIC_RELAXED);
      if (futex_wait(&pi->serial, serial, tsp) < 0 && errno == ETIMEDOUT) return false;
    } else {
      if (!serial_area_) return false;
      uint32_t *s = &serial_area_->hdr()->serial;
      uint32_t serial = load(s, __ATOMIC_RELAXED);
      if (futex_wait(s, serial, tsp) < 0 && errno == ETIMEDOUT) return false;
    }
  }
}

bool SystemProperties::Rebuild(const std::string &name, bool force) {
  (void)force;
  if (!initialized_) return false;

  auto rebuild_area = [](Area *pa) -> bool {
    if (!pa) return true;
    std::vector<Property> props;
    pa->collect(&props);

    Area *tmp = Area::create_anon(pa->size);
    if (!tmp) return false;
    for (const auto &p : props)
      tmp->add(p.name.c_str(), static_cast<uint32_t>(p.name.size()), p.value.c_str(),
               static_cast<uint32_t>(p.value.size()));

    uint32_t used = __atomic_load_n(&tmp->hdr()->bytes_used, __ATOMIC_RELAXED);
    if (used > pa->data_size) used = static_cast<uint32_t>(pa->data_size);
    memcpy(pa->data(), tmp->data(), used);
    if (pa->data_size > used) memset(pa->data() + used, 0, pa->data_size - used);
    __atomic_store_n(&pa->hdr()->bytes_used, used, __ATOMIC_RELAXED);

    munmap(tmp->base, tmp->size);
    delete tmp;
    return true;
  };

  if (presplit_) return rebuild_area(presplit_area_);
  if (!name.empty()) return rebuild_area(AreaForName(name));
  for (uint32_t i = 0; i < areas_.size(); i++) rebuild_area(AreaForIndex(i));
  return true;
}

} // namespace prop
