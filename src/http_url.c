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

struct _nodec_url_t {
  struct http_parser_url parts;
  uv_buf_t    urlmem;
  const char* original;
};

void nodec_url_free(nodec_url_t* url) {
  nodec_bufref_free(&url->urlmem);
  nodec_free(url);
}

void nodec_url_freev(lh_value urlv) {
  nodec_url_free((nodec_url_t*)lh_ptr_value(urlv));
}

static nodec_url_t*  nodecx_parse_urlx(const char* url, bool onlyhost) {
  int err;
  nodec_url_t* nurl = nodec_zero_alloc(nodec_url_t);
  {on_abort(nodec_url_freev, lh_value_ptr(nurl)) {
    // we prepend "http://" if no protocol is specified
    if (strstr(url, "://") == NULL && !onlyhost) {
      uv_buf_t buf = nodec_buf_str(nodec_strdup("http://"));     
      nurl->urlmem = nodec_buf_append_into(buf,nodec_buf_str(url));
    }
    else {
      nurl->urlmem = nodec_buf_str(nodec_strdup(url));
    }
    nurl->original = url;
    // and parse it
    err = http_parser_parse_url(nurl->urlmem.base, nurl->urlmem.len, (onlyhost ? 1 : 0), &nurl->parts);
    if (err == 0) {
      // 0-terminate all fields in our private memory
      for (enum http_parser_url_fields f = 0; f < UF_MAX; f++) {
        if (((1 << f) & nurl->parts.field_set) != 0) {
          size_t ofs = nurl->parts.field_data[f].off;
          size_t len = nurl->parts.field_data[f].len;
          if (f == UF_PATH) { // don't consider starting slash
            ofs++; 
            len--;
          }
          assert(ofs + len <= nurl->urlmem.len);
          nurl->urlmem.base[ofs + len] = 0;
        }
      }
    }
  }}
  if (err != 0) {
    nodec_url_free(nurl);
    return NULL;
  }
  else {
    return nurl;
  }
}

static nodec_url_t*  nodec_parse_urlx(const char* url, bool onlyhost) {
  nodec_url_t* nurl = nodecx_parse_urlx(url, onlyhost);
  if (nurl == NULL) {
    char buf[256];
    snprintf(buf, 256, "invalid url: %s", url);
    lh_throw_strdup(EINVAL, buf);
  }
  return nurl;
}

nodec_url_t*  nodecx_parse_url(const char* url) {
  return nodecx_parse_urlx(url, false);
}

nodec_url_t*  nodecx_parse_host(const char* host) {
  return nodecx_parse_urlx(host, true);
}

nodec_url_t*  nodec_parse_url(const char* url) {
  return nodec_parse_urlx(url, false);
}

nodec_url_t*  nodec_parse_host(const char* host) {
  return nodec_parse_urlx(host,true);
}

static const char* nodec_url_field(const nodec_url_t* url, enum http_parser_url_fields f) {
  if (((1 << f) & url->parts.field_set) == 0) {
    return NULL;
  }
  else {
    size_t ofs = url->parts.field_data[f].off;
    size_t len = url->parts.field_data[f].len;
    if (len > 0 && url->urlmem.base[ofs] == 0) {
      // for things like "path" that include the initial "/"
      ofs++;
      len--;
    }
    assert(ofs + len <= url->urlmem.len);
    assert(url->urlmem.base[ofs + len] == 0);
    return &url->urlmem.base[ofs];
  }
}

const char* nodec_url_schema(const nodec_url_t* url) {
  return nodec_url_field(url, UF_SCHEMA);
}
const char* nodec_url_host(const nodec_url_t* url) {
  return nodec_url_field(url, UF_HOST);
}
const char* nodec_url_path(const nodec_url_t* url) {
  return nodec_url_field(url, UF_PATH);
}
const char* nodec_url_query(const nodec_url_t* url) {
  return nodec_url_field(url, UF_QUERY);
}
const char* nodec_url_fragment(const nodec_url_t* url) {
  return nodec_url_field(url, UF_FRAGMENT);
}
const char* nodec_url_userinfo(const nodec_url_t* url) {
  return nodec_url_field(url, UF_USERINFO);
}
const char* nodec_url_port_str(const nodec_url_t* url) {
  return nodec_url_field(url, UF_PORT);
}

uint16_t nodec_url_port(const nodec_url_t* url) {
  return url->parts.port;
}
bool nodec_url_is_ip6(const nodec_url_t* url) {
  return (strchr(nodec_url_host(url), ':') != NULL);
}
