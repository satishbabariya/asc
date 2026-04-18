// WASI environment / args / cwd bindings for the asc standard library.
//
// Exposes:
//   __asc_env_get      — read an env var into a caller buffer.
//   __asc_env_set      — set/update an env var in the process.
//   __asc_env_remove   — unset an env var.
//   __asc_env_count    — number of env vars in the current snapshot.
//   __asc_env_get_nth  — iterate env vars by index (key + value buffers).
//   __asc_getcwd       — read current working directory.
//   __asc_exe_path     — read current executable path (best effort).
//
// Native builds delegate to POSIX (getenv/setenv/unsetenv, environ, getcwd,
// _NSGetExecutablePath / /proc/self/exe). Wasm builds use WASI
// `environ_get` / `environ_sizes_get` / `args_get` / `args_sizes_get`, which
// expose a point-in-time snapshot — set/remove maintain a small overlay map
// so subsequent `var`/`vars` calls see mutations from the same process.

#ifdef __wasm__

typedef unsigned int __wasi_size_t;

extern int __imported_wasi_snapshot_preview1_environ_get(
    unsigned char **environ, unsigned char *environ_buf)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("environ_get")));

extern int __imported_wasi_snapshot_preview1_environ_sizes_get(
    __wasi_size_t *environ_count, __wasi_size_t *environ_buf_size)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("environ_sizes_get")));

extern int __imported_wasi_snapshot_preview1_args_get(
    unsigned char **argv, unsigned char *argv_buf)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("args_get")));

extern int __imported_wasi_snapshot_preview1_args_sizes_get(
    __wasi_size_t *argc, __wasi_size_t *argv_buf_size)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("args_sizes_get")));

extern void *malloc(unsigned long);
extern void free(void *);
extern void *memcpy(void *, const void *, unsigned long);
extern void *memset(void *, int, unsigned long);

/* Minimal overlay map storing runtime mutations. */
#define ASC_ENV_OVERLAY_MAX 64
static char *__asc_env_overlay_key[ASC_ENV_OVERLAY_MAX];
static char *__asc_env_overlay_val[ASC_ENV_OVERLAY_MAX];
static unsigned int __asc_env_overlay_count = 0;

static int __asc_strlen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}

static int __asc_str_eqn(const char *a, const char *b, unsigned int n) {
  for (unsigned int i = 0; i < n; i++) {
    if (a[i] != b[i]) return 0;
  }
  return 1;
}

static int __asc_overlay_find(const char *key, unsigned int key_len) {
  for (unsigned int i = 0; i < __asc_env_overlay_count; i++) {
    if ((unsigned int)__asc_strlen(__asc_env_overlay_key[i]) == key_len &&
        __asc_str_eqn(__asc_env_overlay_key[i], key, key_len)) {
      return (int)i;
    }
  }
  return -1;
}

static void __asc_overlay_put(const char *key, unsigned int key_len,
                              const char *val, unsigned int val_len) {
  int idx = __asc_overlay_find(key, key_len);
  char *kcopy = 0;
  char *vcopy = 0;
  if (val) {
    vcopy = (char *)malloc(val_len + 1);
    if (!vcopy) return;
    memcpy(vcopy, val, val_len);
    vcopy[val_len] = 0;
  }
  if (idx >= 0) {
    if (__asc_env_overlay_val[idx]) free(__asc_env_overlay_val[idx]);
    __asc_env_overlay_val[idx] = vcopy; /* may be null → tombstone/remove */
    return;
  }
  if (__asc_env_overlay_count >= ASC_ENV_OVERLAY_MAX) {
    if (vcopy) free(vcopy);
    return;
  }
  kcopy = (char *)malloc(key_len + 1);
  if (!kcopy) { if (vcopy) free(vcopy); return; }
  memcpy(kcopy, key, key_len);
  kcopy[key_len] = 0;
  __asc_env_overlay_key[__asc_env_overlay_count] = kcopy;
  __asc_env_overlay_val[__asc_env_overlay_count] = vcopy;
  __asc_env_overlay_count++;
}

/* Iterate the WASI snapshot, invoking visit(key,key_len,val,val_len,ctx).
 * Returns non-zero if visit asks to stop (non-zero return from visit). */
typedef int (*__asc_env_visit_fn)(const char *key, unsigned int key_len,
                                   const char *val, unsigned int val_len,
                                   void *ctx);

static int __asc_snapshot_for_each(__asc_env_visit_fn visit, void *ctx) {
  __wasi_size_t count = 0, buf_size = 0;
  if (__imported_wasi_snapshot_preview1_environ_sizes_get(&count, &buf_size) != 0)
    return 0;
  if (count == 0) return 0;
  unsigned char **ptrs = (unsigned char **)malloc(sizeof(unsigned char *) * count);
  unsigned char *buf = (unsigned char *)malloc(buf_size);
  if (!ptrs || !buf) {
    if (ptrs) free(ptrs);
    if (buf) free(buf);
    return 0;
  }
  int stop = 0;
  if (__imported_wasi_snapshot_preview1_environ_get(ptrs, buf) == 0) {
    for (__wasi_size_t i = 0; i < count && !stop; i++) {
      const char *entry = (const char *)ptrs[i];
      unsigned int klen = 0;
      while (entry[klen] && entry[klen] != '=') klen++;
      unsigned int vlen = 0;
      const char *vptr = entry[klen] == '=' ? entry + klen + 1 : entry + klen;
      while (vptr[vlen]) vlen++;
      stop = visit(entry, klen, vptr, vlen, ctx);
    }
  }
  free(ptrs);
  free(buf);
  return stop;
}

struct __asc_lookup_ctx {
  const char *key;
  unsigned int key_len;
  char *out;
  unsigned int cap;
  unsigned int *out_len;
  int found;
};

static int __asc_lookup_visit(const char *key, unsigned int key_len,
                               const char *val, unsigned int val_len,
                               void *raw) {
  struct __asc_lookup_ctx *ctx = (struct __asc_lookup_ctx *)raw;
  if (key_len != ctx->key_len) return 0;
  if (!__asc_str_eqn(key, ctx->key, key_len)) return 0;
  unsigned int n = val_len < ctx->cap ? val_len : ctx->cap;
  for (unsigned int i = 0; i < n; i++) ctx->out[i] = val[i];
  *(ctx->out_len) = n;
  ctx->found = 1;
  return 1;
}

int __asc_env_get(const char *key, unsigned int key_len,
                  char *buf, unsigned int cap, unsigned int *out_len) {
  /* Overlay takes precedence. */
  int idx = __asc_overlay_find(key, key_len);
  if (idx >= 0) {
    if (!__asc_env_overlay_val[idx]) return -1; /* tombstoned */
    const char *v = __asc_env_overlay_val[idx];
    unsigned int vlen = (unsigned int)__asc_strlen(v);
    unsigned int n = vlen < cap ? vlen : cap;
    for (unsigned int i = 0; i < n; i++) buf[i] = v[i];
    if (out_len) *out_len = n;
    return 0;
  }
  struct __asc_lookup_ctx ctx = { key, key_len, buf, cap, out_len, 0 };
  __asc_snapshot_for_each(__asc_lookup_visit, &ctx);
  return ctx.found ? 0 : -1;
}

int __asc_env_set(const char *key, unsigned int key_len,
                  const char *val, unsigned int val_len) {
  if (!val) {
    __asc_overlay_put(key, key_len, 0, 0);
    return 0;
  }
  __asc_overlay_put(key, key_len, val, val_len);
  return 0;
}

int __asc_env_remove(const char *key, unsigned int key_len) {
  __asc_overlay_put(key, key_len, 0, 0);
  return 0;
}

/* Count snapshot entries that are NOT overridden/removed by the overlay. */
struct __asc_count_ctx {
  unsigned int count;
};

static int __asc_count_visit(const char *key, unsigned int key_len,
                              const char *val, unsigned int val_len,
                              void *raw) {
  (void)val; (void)val_len;
  struct __asc_count_ctx *ctx = (struct __asc_count_ctx *)raw;
  int oidx = __asc_overlay_find(key, key_len);
  if (oidx >= 0 && __asc_env_overlay_val[oidx] == 0) return 0; /* tombstoned */
  ctx->count++;
  return 0;
}

/* Does `key` appear in the current WASI snapshot? */
struct __asc_exists_ctx {
  const char *key;
  unsigned int key_len;
  int found;
};

static int __asc_exists_visit(const char *key, unsigned int key_len,
                               const char *val, unsigned int val_len,
                               void *raw) {
  (void)val; (void)val_len;
  struct __asc_exists_ctx *ctx = (struct __asc_exists_ctx *)raw;
  if (key_len != ctx->key_len) return 0;
  if (!__asc_str_eqn(key, ctx->key, key_len)) return 0;
  ctx->found = 1;
  return 1;
}

unsigned int __asc_env_count(void) {
  /* Snapshot contribution (minus tombstoned keys). */
  struct __asc_count_ctx ctx = { 0 };
  __asc_snapshot_for_each(__asc_count_visit, &ctx);
  unsigned int total = ctx.count;
  /* Overlay inserts that aren't in the snapshot are extra. One O(N) snapshot
   * pass per overlay entry is acceptable because overlay is capped at
   * ASC_ENV_OVERLAY_MAX (64). */
  for (unsigned int i = 0; i < __asc_env_overlay_count; i++) {
    if (__asc_env_overlay_val[i] == 0) continue; /* tombstone already accounted */
    struct __asc_exists_ctx ex = {
      __asc_env_overlay_key[i],
      (unsigned int)__asc_strlen(__asc_env_overlay_key[i]),
      0,
    };
    __asc_snapshot_for_each(__asc_exists_visit, &ex);
    if (!ex.found) total++;
  }
  return total;
}

struct __asc_nth_ctx {
  unsigned int target;
  unsigned int seen;
  char *kbuf;
  unsigned int kcap;
  unsigned int *klen_out;
  char *vbuf;
  unsigned int vcap;
  unsigned int *vlen_out;
  int found;
};

static int __asc_nth_visit(const char *key, unsigned int key_len,
                            const char *val, unsigned int val_len,
                            void *raw) {
  struct __asc_nth_ctx *ctx = (struct __asc_nth_ctx *)raw;
  /* Skip tombstoned keys (overlay value == 0). */
  int oidx = __asc_overlay_find(key, key_len);
  if (oidx >= 0 && __asc_env_overlay_val[oidx] == 0) return 0;
  /* If overlay overrides this key, we'll emit it later from the overlay
   * pass — skip it here to avoid duplicates. */
  if (oidx >= 0) return 0;
  if (ctx->seen == ctx->target) {
    unsigned int kn = key_len < ctx->kcap ? key_len : ctx->kcap;
    for (unsigned int i = 0; i < kn; i++) ctx->kbuf[i] = key[i];
    *(ctx->klen_out) = kn;
    unsigned int vn = val_len < ctx->vcap ? val_len : ctx->vcap;
    for (unsigned int i = 0; i < vn; i++) ctx->vbuf[i] = val[i];
    *(ctx->vlen_out) = vn;
    ctx->found = 1;
    return 1;
  }
  ctx->seen++;
  return 0;
}

int __asc_env_get_nth(unsigned int index,
                      char *kbuf, unsigned int kcap, unsigned int *klen_out,
                      char *vbuf, unsigned int vcap, unsigned int *vlen_out) {
  struct __asc_nth_ctx ctx = {
    index, 0,
    kbuf, kcap, klen_out,
    vbuf, vcap, vlen_out,
    0,
  };
  __asc_snapshot_for_each(__asc_nth_visit, &ctx);
  if (ctx.found) return 0;
  /* Spill into overlay entries (inserts that weren't in snapshot). */
  unsigned int logical = ctx.seen;
  for (unsigned int i = 0; i < __asc_env_overlay_count; i++) {
    if (!__asc_env_overlay_val[i]) continue; /* tombstone */
    struct __asc_exists_ctx ex = {
      __asc_env_overlay_key[i],
      (unsigned int)__asc_strlen(__asc_env_overlay_key[i]),
      0,
    };
    __asc_snapshot_for_each(__asc_exists_visit, &ex);
    if (ex.found) continue; /* snapshot already emitted this key */
    if (logical == index) {
      const char *k = __asc_env_overlay_key[i];
      const char *v = __asc_env_overlay_val[i];
      unsigned int klen = (unsigned int)__asc_strlen(k);
      unsigned int vlen = (unsigned int)__asc_strlen(v);
      unsigned int kn = klen < kcap ? klen : kcap;
      for (unsigned int j = 0; j < kn; j++) kbuf[j] = k[j];
      *klen_out = kn;
      unsigned int vn = vlen < vcap ? vlen : vcap;
      for (unsigned int j = 0; j < vn; j++) vbuf[j] = v[j];
      *vlen_out = vn;
      return 0;
    }
    logical++;
  }
  return -1;
}

int __asc_getcwd(char *buf, unsigned int cap, unsigned int *out_len) {
  /* WASI preview1 exposes no cwd primitive — fallback to PWD if set. */
  char pwd_key[] = "PWD";
  int rc = __asc_env_get(pwd_key, 3, buf, cap, out_len);
  return rc;
}

int __asc_exe_path(char *buf, unsigned int cap, unsigned int *out_len) {
  /* Preview1: read argv[0] via args_get. */
  __wasi_size_t argc = 0, argv_buf_size = 0;
  if (__imported_wasi_snapshot_preview1_args_sizes_get(&argc, &argv_buf_size) != 0)
    return -1;
  if (argc == 0) return -1;
  unsigned char **argv = (unsigned char **)malloc(sizeof(unsigned char *) * argc);
  unsigned char *argv_buf = (unsigned char *)malloc(argv_buf_size);
  if (!argv || !argv_buf) {
    if (argv) free(argv);
    if (argv_buf) free(argv_buf);
    return -1;
  }
  int rc = -1;
  if (__imported_wasi_snapshot_preview1_args_get(argv, argv_buf) == 0) {
    const char *arg0 = (const char *)argv[0];
    unsigned int n = 0;
    while (arg0[n]) n++;
    unsigned int outn = n < cap ? n : cap;
    for (unsigned int i = 0; i < outn; i++) buf[i] = arg0[i];
    if (out_len) *out_len = outn;
    rc = 0;
  }
  free(argv);
  free(argv_buf);
  return rc;
}

#else /* !__wasm__ */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <limits.h>
#endif

extern char **environ;

static void __asc_copy_out(const char *src, unsigned int src_len,
                           char *buf, unsigned int cap, unsigned int *out_len) {
  unsigned int n = src_len < cap ? src_len : cap;
  for (unsigned int i = 0; i < n; i++) buf[i] = src[i];
  if (out_len) *out_len = n;
}

int __asc_env_get(const char *key, unsigned int key_len,
                  char *buf, unsigned int cap, unsigned int *out_len) {
  /* Null-terminate the key on the stack. */
  char kbuf[1024];
  if (key_len >= sizeof(kbuf)) return -1;
  for (unsigned int i = 0; i < key_len; i++) kbuf[i] = key[i];
  kbuf[key_len] = 0;
  const char *v = getenv(kbuf);
  if (!v) return -1;
  unsigned int vlen = (unsigned int)strlen(v);
  __asc_copy_out(v, vlen, buf, cap, out_len);
  return 0;
}

int __asc_env_set(const char *key, unsigned int key_len,
                  const char *val, unsigned int val_len) {
  char kbuf[1024];
  if (key_len >= sizeof(kbuf)) return -1;
  for (unsigned int i = 0; i < key_len; i++) kbuf[i] = key[i];
  kbuf[key_len] = 0;
  if (!val) {
    return unsetenv(kbuf);
  }
  char *vbuf = (char *)malloc(val_len + 1);
  if (!vbuf) return -1;
  for (unsigned int i = 0; i < val_len; i++) vbuf[i] = val[i];
  vbuf[val_len] = 0;
  int rc = setenv(kbuf, vbuf, 1);
  free(vbuf);
  return rc;
}

int __asc_env_remove(const char *key, unsigned int key_len) {
  char kbuf[1024];
  if (key_len >= sizeof(kbuf)) return -1;
  for (unsigned int i = 0; i < key_len; i++) kbuf[i] = key[i];
  kbuf[key_len] = 0;
  return unsetenv(kbuf);
}

unsigned int __asc_env_count(void) {
  unsigned int n = 0;
  if (!environ) return 0;
  while (environ[n]) n++;
  return n;
}

int __asc_env_get_nth(unsigned int index,
                      char *kbuf, unsigned int kcap, unsigned int *klen_out,
                      char *vbuf, unsigned int vcap, unsigned int *vlen_out) {
  if (!environ) return -1;
  unsigned int i = 0;
  while (environ[i] && i < index) i++;
  if (!environ[i]) return -1;
  const char *entry = environ[i];
  unsigned int klen = 0;
  while (entry[klen] && entry[klen] != '=') klen++;
  const char *vptr = entry[klen] == '=' ? entry + klen + 1 : entry + klen;
  unsigned int vlen = (unsigned int)strlen(vptr);
  __asc_copy_out(entry, klen, kbuf, kcap, klen_out);
  __asc_copy_out(vptr, vlen, vbuf, vcap, vlen_out);
  return 0;
}

int __asc_getcwd(char *buf, unsigned int cap, unsigned int *out_len) {
  char tmp[4096];
  if (!getcwd(tmp, sizeof(tmp))) return -1;
  unsigned int n = (unsigned int)strlen(tmp);
  __asc_copy_out(tmp, n, buf, cap, out_len);
  return 0;
}

int __asc_exe_path(char *buf, unsigned int cap, unsigned int *out_len) {
#if defined(__APPLE__)
  char tmp[4096];
  unsigned int size = sizeof(tmp);
  if (_NSGetExecutablePath(tmp, &size) != 0) return -1;
  unsigned int n = (unsigned int)strlen(tmp);
  __asc_copy_out(tmp, n, buf, cap, out_len);
  return 0;
#elif defined(__linux__)
  char tmp[4096];
  long n = readlink("/proc/self/exe", tmp, sizeof(tmp) - 1);
  if (n <= 0) return -1;
  tmp[n] = 0;
  __asc_copy_out(tmp, (unsigned int)n, buf, cap, out_len);
  return 0;
#else
  (void)buf; (void)cap; (void)out_len;
  return -1;
#endif
}

#endif /* __wasm__ */
