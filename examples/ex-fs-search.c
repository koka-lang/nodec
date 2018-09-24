/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include <nodec.h>
#include <sys/stat.h>
#include <limits.h>
#include "examples.h"

#define countof(X) (sizeof(X)/sizeof(X[0]))

#if defined(_WIN32)
// ref: linux sys/stat.h
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

/*-----------------------------------------------------------------
  
-----------------------------------------------------------------*/

typedef enum _fs_type_t {
  FS_OTHER = 0,
  FS_FILE  = 1,
  FS_DIR   = 2
} fs_type_t;

static fs_type_t get_type_x(const char* path) {
  uv_stat_t uv_stat;
  uv_errno_t uv_errno;
  fs_type_t ans = FS_OTHER;
  if ((uv_errno = asyncx_fs_stat(path, &uv_stat)) == 0) {
    if (S_ISREG(uv_stat.st_mode)) {
      ans = FS_FILE;
    }
    else if (S_ISDIR(uv_stat.st_mode)) {
      ans = FS_DIR;
    }
  }
  return ans;
}

#if defined(_WIN32)
static const char* file_paths[] = {
  "d:\\temp\\nodec\\etc\\ssl\\certs\\ca-certificates.crt",  // Debian, Ubuntu, Gentoo, ...
  "d:\\temp\\nodec\\etc\\pki\\tls\\certs\\ca-bundle.crt",   // Fedora, RHEL
  "d:\\temp\\nodec\\etc\\ssl\\ca-bundle.pem",               // OpenSUSE
  "d:\\temp\\nodec\\etc\\pki\\tls\\cacert.pem",             // OpenELEC
  "d:\\temp\\nodec\\etc\\pki\\ca-trust\\extrated\\pem\\tls-ca-bundle.pem" // CentOS, RHEL
};

static const char* dir_paths[] = {
  "d:\\temp\\nodec\\etc\\ssl\\certs",                       // SLES10, SLES11
  "d:\\temp\\nodec\\system\\security\\cacerts",             // Android
  "d:\\temp\\nodec\\usr\\local\\share\\certs",              // FreeBSD
  "d:\\temp\\nodec\\etc\\pki\\tls\\certs",                  // Fedora, RHEL
  "d:\\temp\\nodec\\openssl\\certs"                         // NetBSD
};
#else
static const char* file_paths[] = {
  "/etc/ssl/certs/ca-certificates.crt",               // Debian, Ubuntu, Gentoo, ...
  "/etc/pki/tls/certs/ca-bundle.crt",                 // Fedora, RHEL
  "/etc/ssl/ca-bundle.pem",                           // OpenSUSE
  "/etc/pki/tls/cacert.pem",                          // OpenELEC
  "/etc/pki/ca-trust/extrated/pem/tls-ca-bundle.pem"  // CentOS, RHEL
};

static const char* dir_paths[] = {
  "/etc/ssl/certs",                                   // SLES10, SLES11
  "/system/etc/security/cacerts",                     // Android
  "/usr/local/share/certs",                           // FreeBSD
  "/etc/pki/tls/certs",                               // Fedora, RHEL
  "/etc/openssl/certs"                                // NetBSD
};
#endif

bool ends_with(const char* str, const char* end) {
  if (str && end) {
    size_t len_str = strlen(str);
    size_t len_end = strlen(end);
    if (len_end <= len_str) {
      return strncmp(str + len_str - len_end, end, len_end) == 0;
    }
  }
  return false;
}

bool is_suffix_ok(const char *path) {
  static const char* acceptable_suffixes[] = { ".crt", ".cert", ".pem" };
  size_t i;
  for (i = 0; i < countof(acceptable_suffixes); i++) {
    if (ends_with(path, acceptable_suffixes[i])) {
      return true;
    }
  }
  return false;
}

static bool add_certificate(const char* path) {
  nodec_log_debug("add_certificate \"%s\"", path);
  return true;
}

static bool search_files() {
  size_t i;
  for (i = 0; i < countof(file_paths); i++) {
    const char* path = file_paths[i];
    if (get_type_x(path) == FS_FILE) {
      if (add_certificate(path)) {
        return true;
      }
    }
  }
  return false;
}

static bool search_directory(const char* dir_path) {
  bool ans = false;
  nodec_scandir_t* scandir = async_fs_scandir(dir_path);
  // what if async_fs_scandir throws?
  {using_fs_scandir(scandir) {
    uv_dirent_t dirent;
    while (async_fs_scandir_next(scandir, &dirent) && !ans) {
      // what if async_fs_scandir_next throws?
      if (is_suffix_ok(dirent.name)) {
        char file_path[MAX_PATH + 1];
        file_path[MAX_PATH] = 0;
        int n = snprintf(file_path, MAX_PATH, "%s\\%s", dir_path, dirent.name);
        if (0 < n && n <= MAX_PATH) {
          if (get_type_x(file_path) == FS_FILE) {
            ans = add_certificate(file_path);
          }
        }
      }
    }
  }}
  return ans;
}

static bool search_directories() {
  size_t i;
  for (i = 0; i < countof(dir_paths); i++) {
    const char* dir_path = dir_paths[i];
    if (get_type_x(dir_path) == FS_DIR) {
      if (search_directory(dir_path))
        return true;
    }
  }
  return false;
}

void ex_fs_search() {
  bool ans;
  if (!(ans = search_files())) {
    ans = search_directories();
  }
  nodec_log_debug("%s", ans ? "Success" : "Failure");
}
