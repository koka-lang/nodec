#include "nodec.h"
#include "nodec-primitive.h"
#include "nodec-internal.h"
#include <assert.h> 
#include <malloc.h>

/*-----------------------------------------------------------------
  string/memory utility functions
-----------------------------------------------------------------*/

bool nodec_starts_with(const char* s, const char* prefix) {
  if (s == NULL || prefix == NULL) return false;
  size_t m = strlen(prefix);
  if (m == 0) return true;
  return (strncmp(s, prefix, m) == 0);
}

bool nodec_starts_withi(const char* s, const char* prefix) {
  if (s == NULL || prefix == NULL) return false;
  size_t m = strlen(prefix);
  if (m == 0) return true;
  return (nodec_strnicmp(s, prefix, m) == 0);
}


bool nodec_ends_with(const char* s, const char* pat) {
  if (s == NULL || pat == NULL) return false;
  size_t m = strlen(pat);
  if (m == 0) return true;
  size_t n = strlen(s);
  if (n < m) return false;
  return (strncmp(s + (n - m), pat, m) == 0);
}

bool nodec_ends_withi(const char* s, const char* pat) {
  if (s == NULL || pat == NULL) return false;
  size_t m = strlen(pat);
  if (m == 0) return true;
  size_t n = strlen(s);
  if (n < m) return false;
  return (nodec_strnicmp(s + (n - m), pat, m) == 0);
}

uv_errno_t nodec_strncpy(char* dest, size_t destsz, const char* src, size_t count) {
  if (dest == NULL || destsz == 0) return UV_EINVAL;
  *dest = 0;
  if (count == 0) return 0;
  if (src == NULL) return UV_EINVAL;
  if (destsz < count - 1) return UV_E2BIG;
  while (*src != 0 && count > 0) {
    *dest = *src;
    dest++;
    src++;
    count--;
  }
  *dest = 0;
  return (count > 0 ? UV_E2BIG : 0);
}

int nodec_strnicmp(const char* s, const char* t, size_t n) {
  if (s == NULL) return (t == NULL ? 0 : -1);
  if (t == NULL) return 1;
  if (n == 0) return 0;
  char c;
  char d;
  do {
    c = *s; s++;
    d = *t; t++;
    n--;
    if (c != d) {
      if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
      if (d >= 'A' && d <= 'Z') d = d - 'A' + 'a';
      if (c != d) return (c - d);
    }
  } while (c != 0 && d != 0 && n > 0);
  return 0;
}

int nodec_stricmp(const char* s, const char* t) {
  return nodec_strnicmp(s, t, SIZE_MAX);
}

// Search for byte pattern in byte source array.
const void* nodec_memmem(const void* src, size_t src_len, const void* pat, size_t pat_len)
{
  const char* csrc = (const char*)src;
  const char* cpat = (const char*)pat;
  
  if (src_len==0 || pat_len==0) return NULL;
  if (src_len < pat_len) return NULL;
  if (pat_len==1) return memchr(src, (int)cpat[0], src_len);

  for (size_t i = 0; i < src_len - pat_len + 1; i++) {
    if (csrc[i] == cpat[0] && memcmp(csrc+i, cpat, pat_len) == 0) {
      return csrc+i;
    }
  }
  return NULL;
}

/*-----------------------------------------------------------------
  uv_buf_t functions
-----------------------------------------------------------------*/

uv_buf_t nodec_buf(const void* data, size_t len) {
  return uv_buf_init((char*)data, (uv_buf_len_t)(len));
}

uv_buf_t nodec_buf_str(const char* s) {
  return nodec_buf(s, (s==NULL ? 0 : strlen(s)) );
}

uv_buf_t nodec_buf_strdup(const char* s) {
  return nodec_buf_str(nodec_strdup(s));
}


uv_buf_t nodec_buf_null() {
  return nodec_buf(NULL, 0);
}

uv_buf_t nodec_buf_alloc(size_t len) {
  uv_buf_t buf = nodec_buf(nodec_malloc(len + 1), len);  // always allow one more for zero termination
  buf.base[len] = 0;
  return buf;
}

uv_buf_t nodec_buf_realloc(uv_buf_t buf, size_t len) {
  // if (buf.len >= len) return buf;
  if (len == SIZE_MAX) lh_throw_errno(EOVERFLOW);
  uv_buf_t newbuf = nodec_buf(nodec_realloc(buf.base,len + 1), len);  // always allow one more for zero termination
  newbuf.base[len] = 0;
  return newbuf;
}

void nodec_buf_free(uv_buf_t buf) {
  if (buf.base != NULL) nodec_free(buf.base);
}

void nodec_bufref_free(uv_buf_t* buf) {
  nodec_buf_free(*buf);
  *buf = nodec_buf_null();
}

void nodec_bufref_freev(lh_value bufref) {
  nodec_bufref_free((uv_buf_t*)lh_ptr_value(bufref));  
}

void nodec_bufref_nofreev(lh_value bufref) {
  // do nothing
}

bool nodec_buf_is_null(uv_buf_t buf) {
  return (buf.base == NULL || buf.len == 0);
}

uv_buf_t nodec_buf_ensure_ex(uv_buf_t buf, size_t needed, size_t initial_size, size_t max_increase) {
  if (buf.len >= needed) return buf;  // already big enough
  size_t newlen = 0;
  if (nodec_buf_is_null(buf)) {
    newlen = (initial_size == 0 ? 8*1024 : initial_size);    // 8KB default
  }
  else {
    if (max_increase == 0) max_increase = 4 * 1024 * 1024; // 4MB max increase default
    newlen = buf.len + (buf.len > max_increase ? max_increase : buf.len);  // double up to max increase
  }
  if (newlen < needed)  newlen = needed;   // ensure at least `needed` space
  return nodec_buf_realloc(buf, newlen);  // and reallocate
}

uv_buf_t nodec_buf_ensure(uv_buf_t buf, size_t needed) {
  return nodec_buf_ensure_ex(buf, needed, 0, 0);
}

uv_buf_t nodec_buf_append_into(uv_buf_t buf1, uv_buf_t buf2) {
  size_t needed = buf1.len + buf2.len;
  uv_buf_t buf = nodec_buf_ensure_ex(buf1, needed, needed, 0);
  memcpy(buf.base + buf1.len, buf2.base, buf2.len);
  buf.base[needed] = 0;
  return buf;
}

uv_buf_t nodec_buf_fit(uv_buf_t buf, size_t needed) {
  if (nodec_buf_is_null(buf) || buf.len < needed) {
    return nodec_buf_ensure_ex(buf, needed, needed, 0);
  }
  else if (buf.len > 128 && (needed / 4) * 5 <= buf.len) {
    // more than 128 bytes and more than 20% wasted; we reallocate if possible
    return nodec_buf_realloc(buf, needed);
  }
  else {
    return nodec_buf(buf.base, needed);
  }
}

/*-----------------------------------------------------------------
  Wrappers for malloc
-----------------------------------------------------------------*/
// Set up different allocation functions
static lh_mallocfun*  custom_malloc = NULL;
static lh_callocfun*  custom_calloc = NULL;
static lh_reallocfun* custom_realloc = NULL;
static lh_freefun*    custom_free = NULL;

void nodec_register_malloc(lh_mallocfun* _malloc, lh_callocfun* _calloc, lh_reallocfun* _realloc, lh_freefun* _free) {
  lh_register_malloc(_malloc, _calloc, _realloc, _free);
  custom_malloc = _malloc;
  custom_calloc = _calloc;
  custom_realloc = _realloc;
  custom_free = _free;
}

void  nodec_freev(lh_value p) {
  nodec_free(lh_ptr_value(p));
}

void  nodec_free_bufrefv(lh_value p) {
  uv_buf_t* buf = (uv_buf_t*)lh_ptr_value(p);
  nodec_free(buf->base);
  buf->base = NULL;
  buf->len = 0;
}

void* _nodecx_malloc(size_t size) {
  return (custom_malloc==NULL ? malloc(size) : custom_malloc(size));
}

void* _nodecx_calloc(size_t count, size_t size) {
  return (custom_calloc == NULL ? calloc(count, size) : custom_calloc(count, size));
}

void* _nodecx_realloc(void* p, size_t newsize) {
  return (custom_realloc == NULL ? realloc(p,newsize) : custom_realloc(p,newsize));
}

void  _nodec_free(const void* p) {
  if (p != NULL) {
    if (custom_free == NULL) free((void*)p); else custom_free(p);
  }
}


void* check_nonnull(void* p) {
  if (p == NULL) nodec_check(UV_ENOMEM);
  return p;
}

void* _nodec_malloc(size_t size) {
  return check_nonnull(_nodecx_malloc(size));
}

void* _nodec_calloc(size_t count, size_t size) {
  return check_nonnull(_nodecx_calloc(count,size));
}

void* _nodec_realloc(void* p, size_t newsize) {
  return check_nonnull(_nodecx_realloc(p,newsize));
}

static char* _nodec_strndup(const char* s, size_t max) {
  size_t n = (max == SIZE_MAX ? max : max + 1);
  char* t = nodec_alloc_n(n,char);
  #ifdef _MSC_VER
  strncpy_s(t, n, s, max);
  #else
  strncpy(t, s, max);
  #endif
  t[max] = 0;
  return t;
}

char* nodec_strdup(const char* s) {
  if (s == NULL) return NULL;
  size_t n = strlen(s);
  return _nodec_strndup(s, n);
}

char* nodec_strndup(const char* s, size_t max) {
  if (s == NULL) return NULL;
  return _nodec_strndup(s, max);
}


/*-----------------------------------------------------------------
  Memory checks
-----------------------------------------------------------------*/
void nodec_check_memory() {
  #if defined(_MSC_VER) && !defined(__clang__)
  # if defined(_DEBUG)
  fprintf(stderr, "\nchecked memory leaks.\n");
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
  _CrtDumpMemoryLeaks();
  # endif
  #endif
}
