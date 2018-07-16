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

#define HTTP_MAX_HEADERS (8*1024)

/*-----------------------------------------------------------------
  HTTP Request
-----------------------------------------------------------------*/
#if 0
typedef enum http_method http_method_t;

typedef struct _http_header_t {
  const char* name;
  const char* value;
  bool value_do_free;
} http_header_t;


typedef struct _http_headers_t {
  size_t  count; // how many are there
  size_t  size;  // how big is our array
  const http_header_t* elems;  // realloc on demand, perhaps start with size 8 and do +8 as it comes?  
} http_headers_t;

static void http_headers_add(http_headers_t* headers, const char* name, const char* value) {
  if (name == NULL) return;
  if (headers->count >= headers->size) {
    size_t newsize = (headers->size == 0 ? 16 : 2 * headers->size);
    headers->elems = nodec_realloc_n(headers->elems, newsize, http_header_t);
  }
  http_header_t* h = &headers->elems[headers->count];
  headers->count++;
  h->name = name;
  h->value = value;
  h->value_do_free = false;
}

/*
static void http_headers_free(http_headers_t* headers);


static void http_headers_add(http_headers_t* headers, const char* name, size_t len);
static void http_headers_set(http_headers_t* headers, const char* name, const char* value, size_t len);

static const char* http_headers_lookup(http_headers_t* headers, const char* name);
static size_t http_headers_count(http_headers_t* headers);
static const http_header_t* http_headers_at(http_headers_t* headers);
*/

typedef struct _http_request_t {
  // special fields
  const char*     url;
  
  http_parser     parser;
  http_parser_settings parser_settings;
  const char*     current_field;

  http_headers_t  headers;
  uv_buf_t        prefix;
  uv_buf_t        body;
  bool            headers_complete;
  bool            complete;
} http_request_t;

static void terminate(http_request_t* req, const char* at, size_t len) {
  ((char*)at)[len] = 0;
}

static int on_header_field(http_parser* parser, const char* at, size_t len) {
  http_request_t* req = (http_request_t*)parser->data;
  terminate(req, at, len);
  req->current_field = at;
  return 0;
}

static int on_header_value(http_parser* parser, const char* at, size_t len) {
  http_request_t* req = (http_request_t*)parser->data;
  terminate(req, at, len);
  http_headers_add(&req->headers, req->current_field, at);
  req->current_field = NULL;
  return 0;
}

static int on_body(http_parser* parser, const char* at, size_t len) {
  http_request_t* req = (http_request_t*)parser->data;
  req->body = nodec_buf(at, len);
  return 0;
}

static int on_headers_complete(http_parser* parser) {
  http_request_t* req = (http_request_t*)parser->data;
  req->headers_complete = true;
  return 0;
}

static int on_message_complete(http_parser* parser) {
  http_request_t* req = (http_request_t*)parser->data;
  req->complete = true;
  return 0;
}


static void http_request_free( http_request_t* req) {
  // TODO: free more
  nodec_free(req);
}

static void http_request_freev(lh_value reqv) {
  http_request_free((http_request_t*)lh_ptr_value(reqv));
}

static http_request_t* async_http_request_alloc(uv_stream_t* stream) {
  http_request_t* req = nodec_zero_alloc(http_request_t);
  http_parser_init(&req->parser, HTTP_REQUEST);
  req->parser.data = req;
  http_parser_init_settings(&req->parser_settings);
  req->parser_settings.on_header_field = &on_header_field;
  req->parser_settings.on_header_value = &on_header_value;
  req->parser_settings.on_headers_complete = &on_headers_complete;
  req->parser_settings.on_message_complete = &on_message_complete;
  req->parser_settings.on_body = &on_body;
  {on_abort(http_request_freev, req) {
    nodec_read_start(stream, 0, HTTP_MAX_HEADERS, 0);
    req->prefix = async_read_buf(stream);
    if (req->prefix.base == NULL || req->prefix.len == 0) throw_http_err(HTTP_STATUS_BAD_REQUEST);
    http_parser_execute(&req->parser, &req->parser_settings, req->prefix.base, req->prefix.len);
    if (req->parser.http_errno != 0) throw_http_err(req->parser.http_errno);
    if (!req->headers_complete) throw_http_err(HTTP_STATUS_PAYLOAD_TOO_LARGE);
  }}
  return req;
}

static uv_buf_t async_http_request_read_body_buf(http_request_t* req) {

}


/*
const char*   http_request_url(http_request_t* req);
const char*   http_request_http_version(http_request_t* req);
http_method_t http_request_method(http_request_t* req);
bool          http_request_keep_alive(http_request_t* req);
size_t        http_request_content_length(http_request_t* req);

const char*   http_request_header(http_request_t* req, const char* name);
// .. etc

static void http_request_add_header(http_request_t* req, const char* name, const char* value);
*/

#endif

/*-----------------------------------------------------------------
  HTTP errors|
-----------------------------------------------------------------*/
const char* http_error_headers =
"HTTP/1.1 %i %s\r\n"
"Server : NodeC\r\n"
"Content-Length : %i\r\n"
"Content-Type : text/html; charset=utf-8\r\n"
"Connection : Closed\r\n"
"\r\n";

const char* http_error_body =
"<!DOCTYPE html>"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"</head>\n"
"<body>\n"
"  <p>Error %i (%s)%s%s.</p>\n"
"</body>\n"
"</html>\n";


typedef struct _http_err_reason {
  http_status status;
  const char* reason;
} http_err_reason;

static http_err_reason http_reasons[] = {
  // Use HTTP_STATUS_MAP from <http_parser.h>
  #define XX(num, name, string) { num, #string },
  HTTP_STATUS_MAP(XX)
  #undef XX
  { -1, NULL }
};

static void async_write_http_err(uv_stream_t* client, http_status code, const char* msg) {
  const char* reason = "Unknown";
  for (http_err_reason* r = http_reasons; r->reason != NULL; r++) {
    if (r->status == code) {
      reason = r->reason;
      break;
    }
  }
  char body[256];
  snprintf(body, 255, http_error_body, code, reason, (msg == NULL ? "" : ": "), (msg == NULL ? "" : msg));
  body[255] = 0;
  char headers[256];
  snprintf(headers, 255, http_error_headers, code, reason, strlen(body));
  headers[255] = 0;
  const char* strs[2] = { headers, body };
  fprintf(stderr, "HTTP error: %i (%s): %s\n\n", code, reason, (msg == NULL ? "" : msg));
  async_write_strs(client, strs, 2);
}

void throw_http_err_str(http_status status, const char* msg) {
  lh_throw_str(UV_EHTTP - status, msg);
}

void throw_http_err_strdup(http_status status, const char* msg) {
  lh_throw_strdup(UV_EHTTP - status, msg);
}

void throw_http_err(http_status status) {
  throw_http_err_str(status, NULL);
}


static lh_value async_write_http_exnv(lh_value exnv) {
  lh_exception* exn = (lh_exception*)lh_ptr_value(exnv);
  if (exn == NULL || exn->data == NULL) return lh_value_null;
  uv_stream_t* client = exn->data;
  http_status status = 500;
  if (exn->code < UV_EHTTP && exn->code >(UV_EHTTP - 600)) {
    status = -(exn->code - UV_EHTTP);
  }
  async_write_http_err(client, status, exn->msg);
  return lh_value_null;
}


void async_http_server_at(const struct sockaddr* addr, int backlog, int n, uint64_t timeout, nodec_tcp_servefun* servefun) {
  async_tcp_server_at(addr, backlog, n, timeout, servefun, &async_write_http_exnv);
}