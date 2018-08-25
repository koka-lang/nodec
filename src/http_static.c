/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "nodec.h"
#include "nodec-primitive.h"
#include "nodec-internal.h"
#include <assert.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

/*-----------------------------------------------------------------
Static web server
-----------------------------------------------------------------*/
static implicit_define(http_static_config)

static const http_static_config_t* http_static_config() {
  return (const http_static_config_t*)lh_ptr_value(implicit_get(http_static_config));
}


#define ETAG_LEN  30

static lh_value http_try_send_file(uv_file file, const char* path, lh_value statv) {
  // check errors
  const uv_stat_t* stat = (const uv_stat_t*)lh_ptr_value(statv);
  const http_static_config_t* config = http_static_config();
  if (stat->st_size >= SIZE_MAX) {
    nodec_check(UV_E2BIG);
  }
  size_t size = (size_t)stat->st_size;
  if (config->content_max_size > 0 && size > config->content_max_size) {
    nodec_check(UV_E2BIG);
  }

  // Add Cache-Control
  if (config->cache_control != NULL) {
    http_resp_add_header("Cache-Control", config->cache_control);
  }

  // Add Last-Modified and ETag header
  time_t mtime = (time_t)stat->st_mtim.tv_sec;
  if (config->use_last_modified) {
    http_resp_add_header("Last-Modified", nodec_inet_date(mtime));
  }
  if (config->use_etag) {
    char etag[ETAG_LEN + 1];
    // just modified time and size but no inode to cache properly on clusters
    snprintf(etag, ETAG_LEN + 1, "W/%08lx.%08lxT-%08llxS", stat->st_mtim.tv_sec, stat->st_mtim.tv_nsec, (unsigned long long)stat->st_size);
    http_resp_add_header("ETag", etag);

    // Check If-None-Match headers
    const char* if_none_match = http_req_header("If-None-Match");
    if (if_none_match != NULL) {
      const char* iter = NULL;
      size_t      field_len = 0;
      const char* field;
      while ((field = http_header_next_field(if_none_match, &field_len, &iter)) != NULL) {
        if (field_len == ETAG_LEN && strncmp(field, etag, ETAG_LEN) == 0) {
          // matching etag, return a 304 Not-Modified
          printf("match etag! return 304\n");
          http_resp_send_status(HTTP_STATUS_NOT_MODIFIED);
          return lh_value_int(size);
        }
      }
    }
  }

  // Check If-Modified-Since header
  time_t if_modified_since = 0;
  if (nodec_parse_inet_date(http_req_header("If-Modified-Since"), &if_modified_since)) {
    if (if_modified_since >= mtime) {
      // it has not been modified since, return a 304
      printf("match if-modified-since! return 304\n");
      http_resp_send_status(HTTP_STATUS_NOT_MODIFIED);
      return lh_value_int(size);
    }
  }

  // Send content
  bool compressible = false;
  const char* content_type = nodec_mime_info_from_fname(path,&compressible,NULL);
  nodec_stream_t* stream;

  // If gzip is enabled, and this mime type is compressible, send a VARY header for better caching
  if (config->gzip_min_size < SIZE_MAX && compressible && config->gzip_vary) {
    http_resp_add_header("Vary", "Accept-Encoding");
  }
  
  // Check if we should compress this content
  bool gzip = false;
  if (compressible && size > config->gzip_min_size) { 
    const char* accept_enc = http_req_header("Accept-Encoding");
    gzip = (accept_enc != NULL && strstr(accept_enc, "gzip") != NULL);
  }
  if (gzip) {
    printf("use gzip response\n");
    http_resp_add_header("Content-Encoding", "gzip");
  }

  if (http_req_method() == HTTP_HEAD) {
    // don't send a body on HEAD requests
    http_resp_send_ok();
  }
  else {
    // Otherwise send the body 
    if (gzip) {
      // TODO: check if <filename>.gz exists and use that if possible instead of zipping at runtime
      // send zipped content in chunks (as we don't know the final length)
      stream = http_resp_send_status_body(HTTP_STATUS_OK, NODEC_CHUNKED, content_type);
      stream = as_stream(nodec_zstream_alloc(stream)); // gzip'd stream
    }
    else {
      // send as one body
      stream = http_resp_send_status_body(HTTP_STATUS_OK, size, content_type);
    }
    {using_stream(stream) {
      // read the file in parts of `write_buf_size` length and send as we read
      // TODO: overlap the reading of the next part with the sending of the current part?
      uv_buf_t buf = nodec_buf_alloc(config->read_buf_size);
      size_t nread = 0;
      {using_buf(&buf) {
        while ((nread = async_fs_read_into(file, buf, -1)) > 0) {
          async_write_buf(stream, nodec_buf(buf.base, nread));
        };
      }}
    }}
  }
  return lh_value_int(size);
}

const char* http_static_implicit_exts[] = { "html", "htm", NULL };

static bool http_try_send(const http_static_config_t* config, const char* root, const char* path, const char* ext) {
  // check if it exists
  char fname[MAX_PATH];
  const char* pathsep = (root == NULL ? "" : "/");
  const char* extsep = (ext == NULL ? "" : ".");
  const char* base = (config->implicit_index != NULL && (nodec_ends_with(path, "/") || (path[0] == 0)) ? config->implicit_index : "");
  snprintf(fname, MAX_PATH, "%s%s%s%s%s%s", (root == NULL ? "" : root), pathsep, path, base, extsep, (ext == NULL ? "" : ext));
  if (strlen(fname) == 0) return false;

  uv_stat_t stat;
  uv_errno_t err = asyncx_fs_stat(fname, &stat);
  if (err == UV_ENOENT && ext == NULL && config->implicit_exts != NULL) {
    // not found, try implicit extensions
    bool res = false;
    for (size_t i = 0; !res && config->implicit_exts[i] != NULL; i++) {
      res = http_try_send(config, root, path, config->implicit_exts[i]);
    }
    return res;
  }
  else if (err != 0) {
    // error
    return false;
  }
  else if ((stat.st_mode & S_IFREG) == S_IFREG) {
    // a regular file, send it
    using_async_fs_open(fname, O_RDONLY, 0, &http_try_send_file, lh_value_any_ptr(&stat));
    return true;
  }
  else if ((stat.st_mode & S_IFDIR) == S_IFDIR) {
    // a directory, redirect to the directory by appending a final /
    char dirpath[MAX_PATH];
    snprintf(dirpath, MAX_PATH, (path[0] == 0 ? "/" : "/%s/"), path);
    http_resp_add_header("Location", dirpath);
    http_resp_send_status(HTTP_STATUS_MOVED_PERMANENTLY);
    return true;
  }
  else {
    // some other device or symbolic link
    // TODO: follow symbolic links and redirect?
    return false;
  }
}

void http_serve_static(const char* root, const http_static_config_t* config) {
  http_method_t method = http_req_method();
  if (method == HTTP_GET || method == HTTP_HEAD) {
    static http_static_config_t _default_config = http_static_default_config();
    if (config == NULL) config = &_default_config;
    bool ok = false;
    {using_implicit(lh_value_any_ptr(config), http_static_config) {
      // get request path
      const char* path = http_req_path();
      ok = http_try_send(config, root, path, NULL);
    }}
    if (!ok) http_resp_send_status(HTTP_STATUS_NOT_FOUND);
  }
}