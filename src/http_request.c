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
#include <string.h>

#define HTTP_MAX_HEADERS (8*1024)


#ifdef _MSC_VER
# include <malloc.h>
# define alloca _alloca
#else
# include <alloca.h>
#endif


/*-----------------------------------------------------------------
HTTP Headers
-----------------------------------------------------------------*/


typedef struct _http_header_t {
  const char* name;
  const char* value;
  bool _do_free;
} http_header_t;


typedef struct _http_headers_t {
  size_t  count;         // how many are there
  size_t  size;          // how big is our array
  http_header_t* elems;  // realloc on demand, start with size 16 and double as more comes  
} http_headers_t;

static void http_headers_add(http_headers_t* headers, const char* name, const char* value, bool strdup) {
  if (name == NULL) return;
  if (headers->count >= headers->size) {
    size_t newsize = (headers->size == 0 ? 16 : 2 * headers->size);
    headers->elems = nodec_realloc_n(headers->elems, newsize, http_header_t);
    headers->size = newsize;
  }
  http_header_t* h = &headers->elems[headers->count];
  headers->count++;
  h->name = strdup ? nodec_strdup(name) : name;
  h->value = strdup ? nodec_strdup(value) : value;
  h->_do_free = strdup;
}

static void http_header_clear(http_header_t* header) {
  if (header->_do_free) {
    if (header->name != NULL) nodec_free(header->name);
    if (header->value != NULL) nodec_free(header->value);
  }
  memset(header, 0, sizeof(http_header_t));
}

static void http_headers_clear(http_headers_t* headers) {
  for (size_t i = 0; i < headers->count; i++) {
    http_header_clear(&headers->elems[i]);
  }
  if (headers->elems != NULL) nodec_free(headers->elems);
  memset(headers, 0, sizeof(http_headers_t));
}


// Find a header value and normalize if necessary by appending with commas
static const char* http_headers_lookup_from(http_headers_t* headers, const char* name, size_t from) {
  http_header_t* found = NULL;
  uv_buf_t newvalue = nodec_buf_null();
  for (size_t i = from; i < headers->count; i++) {
    http_header_t* h = &headers->elems[i];
    if (h->name == NULL || h->value == NULL) continue;
    if (nodec_stricmp(name, h->name) == 0) {
      if (found == NULL) {
        // found first entry
        found = h;
      }
      else {
        // found another entry.. we string append into the first entry and NULL this one out
        if (h->value != NULL && h->value[0] != 0) {
          if (nodec_buf_is_null(newvalue)) newvalue = nodec_buf_append_into(newvalue, nodec_buf_str(found->value));
          newvalue = nodec_buf_append_into(newvalue, nodec_buf_str(","));
          newvalue = nodec_buf_append_into(newvalue, nodec_buf_str(h->value));
        }
        http_header_clear(h);  // clear the current entry
      }
    }
  }
  if (found == NULL) return NULL;
  if (!nodec_buf_is_null(newvalue)) {
    // update our found entry with the new appended value
    if (found->_do_free) {
      nodec_free(found->value);
    }
    else {
      found->_do_free = true;
      found->name = nodec_strdup(found->name);
    }
    found->value = newvalue.base;
  }
  return found->value;
}

// Lookup a specific header entry (case insensitive), returning its value or NULL if not found.
static const char* http_headers_lookup(http_headers_t* headers, const char* name) {
  return http_headers_lookup_from(headers, name, 0);
}

// Iterate through all entries. `*iter` should start at 0. Returns NULL if done iterating.
static const char* http_headers_next(http_headers_t* headers, const char** value, size_t* iter) {
  if (value != NULL) *value = NULL;
  if (iter == NULL) return NULL;
  while (*iter < headers->count && headers->elems[*iter].name == NULL) {
    (*iter)++;
  }
  if (*iter >= headers->count) return NULL;
  // normalize the entry by lookup itself directly
  const char* name = headers->elems[*iter].name;
  const char* _value = http_headers_lookup_from(headers, name, *iter);
  if (value != NULL) *value = _value;
  (*iter)++;
  return name;
}

/*-----------------------------------------------------------------
Requests
-----------------------------------------------------------------*/

struct _http_in_t
{
  nodec_bstream_t* stream; // the input stream
  http_parser      parser; // the request parser on the stream
  http_parser_settings parser_settings;

  bool            is_request;
  const char*     url;            // parsed url (for client request)
  http_status_t   status;         // parsed status (for server response)
  const char*     status_info;    // status message
  uint64_t        content_length; // real content length from headers
  http_headers_t  headers; // parsed headers; usually pointing into `prefix`
  uv_buf_t        prefix;  // the initially read buffer that holds all initial headers
  size_t          body_start;     // offset of the body start in the prefix buffer

  uv_buf_t        current_body;   // the last parsed body piece; each on_body pauses the parser so only one is needed
  const char*     current_field;  // during header parsing, holds the last seen header field

  bool            headers_complete;  // true if all initial headers have been parsed
  bool            complete;          // true if the whole message has been parsed
  size_t          body_len;

  nodec_bstream_t* body_stream;
};

// Terminate header fields and values by modifying the read buffer in place. (in `prefix`)
static void terminate(http_in_t* req, const char* at, size_t len) {
  ((char*)at)[len] = 0;
}


static int on_header_field(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  terminate(req, at, len);
  req->current_field = at;
  return 0;
}

static int on_header_value(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  // remove trailing comma and spaces
  while (len > 0 && (at[len - 1] == ',' || at[len - 1] == ' ')) len--;
  terminate(req, at, len);
  // add to the headers
  http_headers_add(&req->headers, req->current_field, at, req->headers_complete); // allocate if the headers are complete as the buffer might have changed
  // treat special headers
  /*
  if (_stricmp(req->current_field, "content-length")==0) {
    long long len = atoll(at);
    // printf("read content-length: %lli\n", len);
    if (len > 0 && len <= SIZE_MAX) req->content_length = (uint64_t)len;
  }
  */
  req->current_field = NULL;
  return 0;
}

static int on_url(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  terminate(req, at, len);
  req->url = at;
  return 0;
}

static int on_status(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  terminate(req, at, len);
  req->status = parser->status_code;
  req->status_info = at;
  return 0;
}


static int on_body(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  terminate(req, at, len); // TODO: I think this is always safe since stream reads always over allocate by 1...
  req->current_body = nodec_buf((char*)at, len);  // remember this body piece
  http_parser_pause(parser, 1);             // and pause the parser, returning from execute! (with parser->errno set to HPE_PAUSED)
  return 0;
}

static int on_headers_complete(http_parser* parser) {
  http_in_t* req = (http_in_t*)parser->data;
  req->headers_complete = true;
  if ((parser->flags & F_CONTENTLENGTH) == F_CONTENTLENGTH) {
    req->content_length = parser->content_length;
  }
  //http_parser_pause(parser, 1);             // and pause the parser, returning from execute! (with parser->errno set to HPE_PAUSED)
  return 0;
}

static int on_message_complete(http_parser* parser) {
  http_in_t* req = (http_in_t*)parser->data;
  req->complete = true;
  return 0;
}

// Clear and free all members of a request
void http_in_clear(http_in_t* req) {
  http_headers_clear(&req->headers);
  if (req->body_stream != NULL && req->body_stream != req->stream) {
    nodec_stream_free(as_stream(req->body_stream));
  }
  if (req->prefix.base != NULL) {
    nodec_buf_free(req->prefix);
  }
  memset(req, 0, sizeof(http_in_t));
  // don't free the stream, it is not owned by us
}

void http_in_clearv(lh_value reqv) {
  http_in_clear((http_in_t*)lh_ptr_value(reqv));
}

static enum http_errno check_http_errno(http_parser* parser) {
  enum http_errno err = HTTP_PARSER_ERRNO(parser);
  if (err != HPE_OK && err != HPE_PAUSED) {  // pausing is ok!
    throw_http_err_str(HTTP_STATUS_BAD_REQUEST, http_errno_description(err));
  }
  return err;
}

void http_in_init(http_in_t* in, nodec_bstream_t* stream, bool is_request)
{
  memset(in, 0, sizeof(http_in_t));
  in->stream = stream;
  in->is_request = is_request;
}

static nodec_bstream_t* http_in_stream_alloc(http_in_t* req);
static nodec_stream_t* nodec_cstream_alloc(uint64_t* content_len, nodec_stream_t* s);

size_t async_http_in_read_headers(http_in_t* in ) 
{
  // and read until the double empty line is seen  (\r\n\r\n); the end of the headers 
  size_t headers_len = 0;
  uv_buf_t buf = async_read_buf_upto(in->stream, "\r\n\r\n", 4, HTTP_MAX_HEADERS);
  headers_len = (size_t)buf.len;
  if (nodec_buf_is_null(buf) || headers_len > HTTP_MAX_HEADERS) {
    if (!nodec_buf_is_null(buf)) nodec_buf_free(buf);
    if (headers_len == 0) {
      // eof; means client closed the stream; no problem
      fprintf(stderr, "stream closed while reading headers\n");
      return 0;
    }
    throw_http_err((headers_len > HTTP_MAX_HEADERS ? HTTP_STATUS_PAYLOAD_TOO_LARGE : HTTP_STATUS_BAD_REQUEST));
  }
  //printf("\n\nraw prefix read: %s\n\n\n", buf.base);  // only print idx length here

  // only if successful initialize a request object
  in->prefix = buf;
  http_parser_init(&in->parser, (in->is_request ? HTTP_REQUEST : HTTP_RESPONSE));
  in->parser.data = in;
  http_parser_settings_init(&in->parser_settings);
  in->parser_settings.on_header_field = &on_header_field;
  in->parser_settings.on_header_value = &on_header_value;
  in->parser_settings.on_headers_complete = &on_headers_complete;
  in->parser_settings.on_message_complete = &on_message_complete;
  in->parser_settings.on_body = &on_body;
  in->parser_settings.on_url = &on_url;
  in->parser_settings.on_status = &on_status;

  // parse the headers
  size_t nread = http_parser_execute(&in->parser, &in->parser_settings, in->prefix.base, 
                    headers_len // do not use in->prefix.len because requests can be pipe-lined.
                  );
  check_http_errno(&in->parser);
  assert(nread == headers_len);
  assert(nodec_buf_is_null(in->current_body));
  assert(in->headers_complete);

  // remember where we are at in the current buffer for further body reads 
  in->body_start = nread;
  in->body_stream = in->stream;
  bool chunked = false;
  if (http_in_header_contains(in, "Transfer-Encoding", "chunked")) {
#ifndef NDEBUG
    fprintf(stderr, "use chunked!\n");
#endif
    chunked = true;
    in->body_stream = nodec_bstream_alloc_on( nodec_cstream_alloc( &in->content_length, as_stream(in->body_stream)) );
  }
  if (http_in_header_contains(in, "Content-Encoding", "gzip")) {
#ifndef NDEBUG
    fprintf(stderr, "use gzip!\n");
#endif
    in->body_stream = nodec_zstream_alloc( as_stream(in->body_stream), chunked);
  }  
  return headers_len;
}

/*-----------------------------------------------------------------
 Chunked streams
-----------------------------------------------------------------*/
typedef enum _chunk_parse_state_t{
  CH_HEADER,
  CH_HEADER_LEN,
  CH_HEADER_SKIP_EXT,
  CH_HEADER_LF,
  CH_DATA,
  CH_DATA_END,
  CH_DATA_ENDLF,
  CH_TRAILER,
  CH_TRAILER_SKIP,
  CH_TRAILER_SKIPLF,
  CH_TRAILER_LF,
  CH_END
} chunk_parse_state_t;

typedef struct _chunk_state_t {
  chunk_parse_state_t parse;
  size_t      body_len;
  uv_buf_t    part;
  const char* next;
  const char* end;
  const char* errormsg;
} chunk_state_t;

typedef enum _chunk_parse_result_t {
  CP_EOF,
  CP_NEEDMORE,
  CP_BODYPART,
  CP_ERR
} chunk_parse_result_t;

void chunk_parse_add(chunk_state_t* s, uv_buf_t buf) {
  assert(s->next >= s->end);
  s->next = buf.base;
  s->end = s->next + buf.len;
}

uv_buf_t chunk_parse_part(chunk_state_t* s) {
  assert(!nodec_buf_is_null(s->part));
  return s->part;
}

chunk_parse_result_t chunk_parse(chunk_state_t* s) {
  if (s->errormsg != NULL) return CP_ERR;
  while (true) {
    char c = 0;
    switch (s->parse) {
    case CH_HEADER: {
      s->body_len = 0;
      s->part = nodec_buf_null();
      // fall through
    }
    case CH_HEADER_LEN: {
      if (s->next >= s->end) return CP_NEEDMORE;
      c = *(s->next); s->next++;
      size_t d = 16;
      if (c >= '0' && c <= '9') d = (c - '0');
      else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
      else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
      else if (s->parse == CH_HEADER) return CP_ERR;
      else if (c == '\r') s->parse = CH_HEADER_LF;
      else s->parse = CH_HEADER_SKIP_EXT;
      if (d <= 15) {
        size_t newlen = s->body_len * 16 + d;
        if (newlen < s->body_len) {
          s->errormsg = "chunked part too large";
          return CP_ERR;
        }
        s->body_len = newlen;
        s->parse = CH_HEADER_LEN;
      }
      break;
    }
    case CH_HEADER_SKIP_EXT: {
      if (s->next >= s->end) return CP_NEEDMORE;
      c = *(s->next); s->next++;
      // TODO: should ignore \r inside quoted extension values
      if (c == '\r') s->parse = CH_HEADER_LF;
      break;
    }
    case CH_HEADER_LF: {
      if (s->next >= s->end) return CP_NEEDMORE;
      c = *(s->next); s->next++;
      if (c != '\n') {
        s->errormsg = "invalid chunked header (expecting LF)";
        return CP_ERR;
      }
      s->parse = (s->body_len == 0 ? CH_TRAILER : CH_DATA);
      break;
    }
    case CH_DATA: {
      if (s->body_len == 0) {
        s->parse = CH_DATA_END;
        break;
      }
      else {
        if (s->next >= s->end) return CP_NEEDMORE;
        const char* part = s->next;
        size_t available = (s->end - s->next);
        size_t partlen = (available >= s->body_len ? s->body_len : available);
        s->body_len -= partlen;
        s->next += partlen;
        s->part = nodec_buf(part, partlen);
        return CP_BODYPART;
      }
    }
    case CH_DATA_END: {
      if (s->next >= s->end) return CP_NEEDMORE;
      c = *(s->next); s->next++;
      if (c != '\r') {
        s->errormsg = "invalid chunked data end (expecting CR)";
        return CP_ERR;
      }
      s->parse = CH_DATA_ENDLF;
      break;
    }
    case CH_DATA_ENDLF: {
      if (s->next >= s->end) return CP_NEEDMORE;
      c = *(s->next); s->next++;
      if (c != '\n') {
        s->errormsg = "invalid chunked data end (expecting LF)";
        return CP_ERR;
      }
      s->parse = CH_HEADER;
      break;
    }
    case CH_TRAILER: {
      if (s->next >= s->end) return CP_NEEDMORE;
      c = *(s->next); s->next++;
      s->parse = (c == '\r' ? CH_TRAILER_LF : CH_TRAILER_SKIP);
      break;
    }
    case CH_TRAILER_SKIP: {
      if (s->next >= s->end) return CP_NEEDMORE;
      c = *(s->next); s->next++;
      // TODO: ignore \r inside quoted trailer values
      if (c == '\r') s->parse = CH_TRAILER_SKIPLF;
      break;
    }
    case CH_TRAILER_SKIPLF: {
      if (s->next >= s->end) return CP_NEEDMORE;
      c = *(s->next); s->next++;
      if (c != '\n') {
        s->errormsg = "invalid chunked trailer (expecting LF)";
        return CP_ERR;
      }
      s->parse = CH_TRAILER;
      break;
    }
    case CH_TRAILER_LF: {
      if (s->next >= s->end) return CP_NEEDMORE;
      c = *(s->next); s->next++;
      if (c != '\n') {
        s->errormsg = "invalid chunked ending (expecting LF)";
        return CP_ERR;
      }
      s->parse = CH_END;
      //s->next = NULL;
      //s->end = NULL;
      return CP_EOF;
    }
    case CH_END: {
      return CP_EOF;
    }
    default: {
      return CP_ERR;
    }
    } // switch
  } // while
}

#define MAX_CHUNK_HEADER (100)

typedef struct _nodec_cstream_t {
  nodec_stream_t  stream;
  nodec_stream_t* source;
  uv_buf_t        current;        // the last read buffer
  bool            owned;
  chunk_state_t   parse;
  uint64_t*       content_len;
} nodec_cstream_t;

static uv_buf_t async_cstream_read_bufx(nodec_stream_t* stream, bool* owned) {
  nodec_cstream_t* cs = (nodec_cstream_t*)stream;
  if (owned != NULL) *owned = false;
  chunk_parse_result_t res;
  while ((res = chunk_parse(&cs->parse)) == CP_NEEDMORE) {
    if (cs->owned) nodec_buf_free(cs->current);
    cs->current = async_read_bufx(cs->source, &cs->owned);
    if (nodec_buf_is_null(cs->current)) {
      return cs->current;
    }
    chunk_parse_add(&cs->parse, cs->current);
  }
  assert(res != CP_NEEDMORE);

  if (res == CP_EOF) {
    return nodec_buf_null();
  }
  else if (res == CP_BODYPART) {
    uv_buf_t part = chunk_parse_part(&cs->parse);
    if (cs->content_len != NULL) *cs->content_len += part.len;
    if (part.base == cs->current.base && part.len == cs->current.len) {
      // hand over our current buffer with ownership
      cs->current = nodec_buf_null();
      if (owned) *owned = cs->owned;
      return part;
    }
    else {
      // owned is false
      return part;
    }
  }
  else {
    nodec_throw_msg(UV_EINVAL, (cs->parse.errormsg != NULL ? cs->parse.errormsg : "chunked read error"));
    return nodec_buf_null();
  }
}

static void async_cstream_write_bufs(nodec_stream_t* stream, uv_buf_t bufs[], size_t count) {
  nodec_cstream_t* cs = (nodec_cstream_t*)stream;
  async_write_bufs(cs->source, bufs, count);
}

static void async_cstream_shutdown(nodec_stream_t* stream) {
  // nothing to do
}

static void async_cstream_free(nodec_stream_t* stream) {
  nodec_cstream_t* cs = (nodec_cstream_t*)stream;
  if (cs->owned) nodec_buf_free(cs->current);
  nodec_free(cs);
}

nodec_stream_t* nodec_cstream_alloc(uint64_t* content_len, nodec_stream_t* source) {
  nodec_cstream_t* cs = nodec_zero_alloc(nodec_cstream_t);
  nodec_stream_init(&cs->stream,
    &async_cstream_read_bufx, &async_cstream_write_bufs,
    &async_cstream_shutdown, &async_cstream_free);
  cs->source = source;
  cs->content_len = content_len;
  if (cs->content_len != NULL) *cs->content_len = 0;
  return &cs->stream;
}



/*-----------------------------------------------------------------
Reading the body of a request
-----------------------------------------------------------------*/


nodec_bstream_t* http_in_body(http_in_t* in) {
  return in->body_stream;
}

// Read asynchronously the entire body of the request. 
// The caller is responsible for buffer deallocation.
// Uses Content-Length is possible to read directly into a continuous buffer without reallocation.
uv_buf_t async_http_in_read_body(http_in_t* req, size_t read_max) {
  uv_buf_t  buf = nodec_buf_null();
  nodec_bstream_t* stream = http_in_body(req);
  if (req->complete || req->body_stream == NULL) return buf;
  //{using_bstream(stream) {
     size_t clen = http_in_content_length(req);
     if (clen > read_max) clen = read_max;
     if (clen > 0 && http_in_header(req, "Content-Encoding") == NULL) {
       buf = nodec_buf_alloc(clen);
       {using_buf_on_abort_free(&buf){
          size_t nread = async_read_into(stream, buf);
          nodec_buf_fit(buf, nread);
       }}       
     }
     else {
       buf = async_read_buf_all(stream, read_max);
     }
  //}}
  nodec_stream_free(as_stream(req->body_stream));
  req->body_stream = NULL;
  return buf;
}


/*-----------------------------------------------------------------
Helpers
-----------------------------------------------------------------*/

// Return the read only request URL (only valid on server requests)
const char* http_in_url(http_in_t* req) {
  return req->url;
}

// Return the read only HTTP Status (only valid on server responses)
http_status_t http_in_status(http_in_t* req) {
  return req->status;
}

const char* http_in_status_info(http_in_t* req) {
  return req->status_info;
}

uint16_t http_in_version(http_in_t* req) {
  return (req->parser.http_major << 8 | req->parser.http_minor);
}

// Return the read only request HTTP method (only valid on server requests)
http_method_t http_in_method(http_in_t* req) {
  return (http_method_t)(req->parser.method);
}

uint64_t http_in_content_length(http_in_t* req) {
  return req->content_length;
}

const char* http_in_header(http_in_t* req, const char* name) {
  return http_headers_lookup(&req->headers, name);
}

const char* http_in_header_next(http_in_t* req, const char** value, size_t* iter) {
  return http_headers_next(&req->headers, value, iter);
}

bool http_in_header_contains(http_in_t* req, const char* name, const char* pattern ) {
  const char* s = http_in_header(req,name);
  return (s != NULL && pattern != NULL && strstr(s, pattern) != NULL);
}

const char* http_header_next_field(const char* header, size_t* len, const char** iter) {
  *len = 0;
  if (*iter == NULL) *iter = header;

  // find start
  const char* p = *iter;
  while (*p == ' ' || *p == ',') p++;
  if (*p == '"') p++;
  if (*p == 0) return NULL;

  // find end
  const char* q = p + 1;
  while (*q != 0 && *q != '"' && *q != ' ' && *q != ',') q++;
  *len = (q - p);
  if (*q != 0) q++;
  *iter = q;
  return p;
}



/*-----------------------------------------------------------------
   HTTP response
-----------------------------------------------------------------*/
struct _http_out_t {
  nodec_stream_t* stream;
  uv_buf_t         head;
  size_t           head_offset;
  bool             status_sent;
};

void http_out_init(http_out_t* out, nodec_stream_t* stream) {
  memset(out, 0, sizeof(http_out_t));
  out->stream = stream;
}

void http_out_init_server(http_out_t* out, nodec_stream_t* stream, const char* server_name) {
  http_out_init(out, stream);
  http_out_add_header(out, "Server", server_name);
}

void http_out_init_client(http_out_t* out, nodec_stream_t* stream, const char* host_name) {
  http_out_init(out, stream);
  http_out_add_header(out, "Host", host_name);
}


void http_out_clear(http_out_t* out) {
  nodec_bufref_free(&out->head);
  out->head_offset = 0;
}

void http_out_clearv(lh_value respv) {
  http_out_clear((http_out_t*)lh_ptr_value(respv));
}

void http_out_add_header(http_out_t* out, const char* field, const char* value) {
  size_t n = strlen(field);
  size_t m = strlen(value);
  size_t extra = n + m + 4; // : \r\n
  out->head = nodec_buf_ensure(out->head, out->head_offset + extra);
  char* p = out->head.base + out->head_offset;
  size_t available = out->head.len - out->head_offset;
  nodec_strncpy(p, available, field, n);
  p[n] = ':';
  p[n + 1] = ' ';
  nodec_strncpy(p + n + 2, available - n - 2, value, m);
  nodec_strncpy(p + n + m + 2, available - n - m - 2, "\r\n", 2);
  out->head_offset += extra;
}

/*
static void http_out_add_header_buf(http_out_t* out, uv_buf_t buf) {
  if (nodec_buf_is_null(buf)) return;
  out->head = nodec_buf_ensure(out->head, out->head_offset + buf.len);
  size_t available = out->head.len - out->head_offset;
  memcpy(out->head.base + out->head_offset, buf.base, buf.len);
  out->head_offset += buf.len;
}

static void http_out_send_raw_str(http_out_t* out, const char* s) {
  async_write(out->stream, s);
}
*/

static void http_out_send_raw_headers(http_out_t* out, uv_buf_t prefix, uv_buf_t postfix) {
  uv_buf_t buf = nodec_buf(out->head.base, out->head_offset);
  uv_buf_t bufs[3] = { prefix, buf, postfix };
#ifndef NDEBUG
  fprintf(stderr, "%s%s%s\n", prefix.base, buf.base, postfix.base);
#endif
  async_write_bufs(out->stream, bufs, 3);
  nodec_bufref_free(&out->head);
  out->head_offset = 0;
  out->status_sent = true;
}

static void http_out_send_headers(http_out_t* out, const char* prefix, const char* postfix) {
  http_out_send_raw_headers(out, nodec_buf_str(prefix), nodec_buf_str(postfix));
}

static void http_out_send_status_headers(http_out_t* out, http_status_t status) {
  // send status to a client
  if (status == 0) status = HTTP_STATUS_OK;
  char line[256];
  snprintf(line, 256, "HTTP/1.1 %i %s\r\nDate: %s\r\n", status, nodec_http_status_str(status), nodec_inet_date_now());
  line[255] = 0;
  http_out_send_headers(out, line, "\r\n");
}

static void http_out_send_request_headers(http_out_t* out, http_method_t method, const char* url) {
  // send request to a server
  char prefix[512];
  snprintf(prefix, 512, "%s %s HTTP/1.1\r\nDate: %s\r\n", nodec_http_method_str(method), url, nodec_inet_date_now());
  prefix[511] = 0;
  http_out_send_headers(out, prefix, "\r\n");
}

static void http_out_add_header_content_type(http_out_t* out, const char* content_type) {
  if (content_type == NULL) return;
  char hvalue[256];
  // lookup default character set
  const char* charset = NULL;
  if (content_type != NULL && strstr(content_type, "charset") == NULL) {
    nodec_info_from_mime(content_type, NULL, NULL, &charset);
  }
  const char* charset_sep = (charset == NULL ? "" : ";");
  if (charset == NULL) charset = "";
  snprintf(hvalue, 256, "%s%s%s", content_type, charset_sep, charset);
  http_out_add_header(out, "Content-Type", hvalue);
}


static void http_out_add_headers_body(http_out_t* out, size_t content_length, const char* content_type) {
  http_out_add_header_content_type(out,content_type);
  if (content_length != NODEC_CHUNKED) {
    char lvalue[64];
    snprintf(lvalue, 64, "%zu", content_length);
    http_out_add_header(out, "Content-Length", lvalue);
  }
  else {
    http_out_add_header(out, "Transfer-Encoding", "chunked");
  }
}

bool http_out_status_sent(http_out_t* out) {
  return out->status_sent;
}

/*-----------------------------------------------------------------
  HTTP out body stream
-----------------------------------------------------------------*/

typedef struct _http_out_stream_t {
  nodec_stream_t    stream;
  nodec_stream_t*   source;
  bool              chunked;
} http_out_stream_t;

static void _http_out_write_bufs(nodec_stream_t* stream, uv_buf_t bufs[], size_t count) {
  if (bufs == NULL || count == 0) return;
  http_out_stream_t* hs = (http_out_stream_t*)stream;
  if (!hs->chunked) {
    async_write_bufs(hs->source, bufs, count);
  }
  else {
    // pre and post fix the buffers, and calculate the total
    uv_buf_t* xbufs = alloca((count + 2) * sizeof(uv_buf_t));
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
      total += bufs[i].len;
      if (total < bufs[i].len) nodec_check(EOVERFLOW);
      xbufs[i + 1] = bufs[i];
    }
    // prevent terminating the stream pre-maturely and don't write 0-length chunks
    if (total == 0) return;
    // create pre- and postfix
    char prefix[256];
    snprintf(prefix, 256, "%zX\r\n", total );  // hexadecimal chunk length
    xbufs[0] = nodec_buf(prefix, strlen(prefix));
    xbufs[count + 1] = nodec_buf_str("\r\n");
    // and write it out as a chunk
    async_write_bufs(hs->source, xbufs, count + 2);
  }
}

static void _http_out_shutdown(nodec_stream_t* stream) {
  http_out_stream_t* hs = (http_out_stream_t*)stream;
  if (hs->chunked) {
    // write out final 0 chunk
    async_write_buf(hs->source, nodec_buf_str("0\r\n\r\n"));
  }
}

static void _http_out_free(nodec_stream_t* stream) {
  http_out_stream_t* hs = (http_out_stream_t*)stream;
  hs->source = NULL;  // we don't own the underlying TCP stream, don't free it
  nodec_stream_release(&hs->stream);
  nodec_free(hs);
}

static nodec_stream_t* http_out_stream_alloc(nodec_stream_t* source, bool chunked) {
  http_out_stream_t* hs = nodec_alloc(http_out_stream_t);
  hs->source = source;
  hs->chunked = chunked;
  nodec_stream_init(&hs->stream, NULL,
                       &_http_out_write_bufs, &_http_out_shutdown, &_http_out_free);
  return &hs->stream;
}



void http_out_send_status(http_out_t* out, http_status_t status) {
  http_out_add_header(out, "Content-Length", "0");
  http_out_send_status_headers(out, status);
}

nodec_stream_t* http_out_send_status_body(http_out_t* out, http_status_t status, size_t content_length, const char* content_type) {
  http_out_add_headers_body(out, content_length, content_type);
  http_out_send_status_headers(out, status);
  return http_out_stream_alloc(out->stream, (content_length == NODEC_CHUNKED));
}

void http_out_send_request(http_out_t* out, http_method_t method, const char* url) {
  http_out_add_header(out,"Content-Length", "0");
  http_out_send_request_headers(out, method, url);
}

nodec_stream_t* http_out_send_request_body(http_out_t* out, http_method_t method, const char* url, size_t content_length, const char* content_type) {
  http_out_add_headers_body(out, content_length, content_type);
  http_out_send_request_headers(out, method, url);
  return http_out_stream_alloc(out->stream, (content_length == NODEC_CHUNKED));
}




/*-----------------------------------------------------------------
  HTTP server function
-----------------------------------------------------------------*/

implicit_define(http_current_req)
implicit_define(http_current_resp)
implicit_define(http_current_strand_id)
implicit_define(http_current_url)

void nodec_http_serve(int id, nodec_bstream_t* client, lh_value servefunv) {
  nodec_http_servefun* servefun = ((nodec_http_servefun*)lh_fun_ptr_value(servefunv));
  {using_implicit(lh_value_int(id), http_current_strand_id) {
    http_in_t http_in;
    http_in_init(&http_in, client, true);
    {using_implicit_defer(http_in_clearv, lh_value_any_ptr(&http_in), http_current_req) {
      http_out_t http_out;
      http_out_init_server(&http_out, as_stream(client), "NodeC/0.1");
      {using_implicit_defer(http_out_clearv, lh_value_any_ptr(&http_out), http_current_resp) {
        fprintf(stderr,"\n------------------------------\nstrand %i, read headers\n", id);
        if (async_http_in_read_headers(&http_in) > 0) { // if not closed by client
          char urlpath[1024];
          snprintf(urlpath, 1024, "http://%s%s", http_in_header(&http_in, "Host"), http_in_url(&http_in));
          const nodec_url_t* url = nodec_parse_url(urlpath);
          {using_implicit_defer(nodec_url_freev, lh_value_ptr(url), http_current_url) {
            servefun();
          }}
        }
      }}
    }}
  }}
}


void async_http_server_at(const char* host, tcp_server_config_t* config, nodec_http_servefun* servefun)
{
  tcp_server_config_t default_config = tcp_server_config();
  if (config == NULL) config = &default_config;
  struct sockaddr* addr = nodec_parse_sockaddr(host);
  {using_sockaddr(addr) {
    async_tcp_server_at(addr, config, &nodec_http_serve,
      &async_write_http_exnv, lh_value_fun_ptr(servefun));   // by address to prevent conversion between object and function pointer
  }}
}

lh_value async_http_connect_on(const char* host, nodec_bstream_t* connection, http_connect_fun* connectfun, lh_value arg) {
  lh_value result;
  http_in_t in;
  http_in_init(&in, connection, false);
  {defer(http_in_clearv, lh_value_any_ptr(&in)) {
    http_out_t out;
    http_out_init_client(&out, as_stream(connection), host);
    {defer(http_out_clearv, lh_value_any_ptr(&out)) {
      result = connectfun(&in, &out, arg);
    }}
  }}
  return result;
}

lh_value async_http_connect(const char* url, http_connect_fun* connectfun, lh_value arg) {
  lh_value result = lh_value_null;
  nodec_bstream_t* conn = async_tcp_connect(url);
  {using_bstream(conn) {
    const nodec_url_t* u = nodec_parse_url(url);
    {using_url(u) {
      const char* host = nodec_url_host(u);
      async_http_connect_on(host, conn, connectfun, arg);
    }}
  }}
  return result;
}


/*-----------------------------------------------------------------
HTTP server implicitly bound request and response
-----------------------------------------------------------------*/


int http_strand_id() {
  return lh_int_value(implicit_get(http_current_strand_id));
}

http_in_t*  http_req() {
  return (http_in_t*)lh_ptr_value(implicit_get(http_current_req));
}

http_out_t* http_resp() {
  return (http_out_t*)lh_ptr_value(implicit_get(http_current_resp));
}

const nodec_url_t* http_req_parsed_url() {
  return (const nodec_url_t*)lh_ptr_value(implicit_get(http_current_url));
}

// Responses

void http_resp_add_header(const char* field, const char* value) {
  http_out_add_header(http_resp(), field, value);
}

void http_resp_send_status(http_status_t status) {
  http_out_t* resp = http_resp();
  http_out_send_status(resp, status);
}

bool http_resp_status_sent() {
  return http_out_status_sent(http_resp());
}

nodec_stream_t* http_resp_send_status_body(http_status_t status, size_t content_length, const char* content_type) {
  http_out_t* resp = http_resp();
  return http_out_send_status_body(resp, status, content_length, content_type);
}


void http_resp_send_ok() {
  http_resp_send_status(HTTP_STATUS_OK);
}

void http_resp_send_body_buf(http_status_t status, uv_buf_t buf, const char* content_type) {
  http_out_t* resp = http_resp();
  if (nodec_buf_is_null(buf)) {
    http_out_send_status(resp, status);
  }
  else {
    nodec_stream_t* stream = http_out_send_status_body(resp, status, buf.len, content_type);
    {using_stream(stream) {
      async_write_buf(stream, buf);
    }}
  }
}

void http_resp_send_body_str(http_status_t status, const char* body /* can be NULL */, const char* content_type) {
  http_resp_send_body_buf(status, nodec_buf_str(body), content_type);
}




// Requests

const char* http_req_url() {
  return http_in_url(http_req());
}

http_method_t http_req_method() {
  return http_in_method(http_req());
}

const char* http_req_path() {
  return nodec_url_path(http_req_parsed_url());
}

uint64_t http_req_content_length() {
  return http_in_content_length(http_req());
}

const char* http_req_header(const char* name) {
  return http_in_header(http_req(), name);
}

const char* http_req_header_next(const char** value, size_t* iter) {
  return http_in_header_next(http_req(), value, iter);
}

nodec_bstream_t* async_req_body() {
  return http_in_body(http_req());
}

// Read the full body; the returned buf should be deallocated by the caller.
uv_buf_t async_req_read_body(size_t read_max) {
  return async_http_in_read_body(http_req(), read_max);
}

// Read the full body as a string. Only works if the body cannot contain 0 characters.
const char* async_req_read_body_str(size_t read_max) {
  return async_req_read_body(read_max).base;
}



static void http_in_headers_print(http_in_t* in) {
  size_t iter = 0;
  const char* value;
  const char* name;
  while ((name = http_in_header_next(in, &value, &iter)) != NULL) {
    printf(" %s: %s\n", name, value);
  }
  uv_buf_t buf = async_http_in_read_body(in, 4 * NODEC_MB);
  printf(" computed content-length: %zu\n", (size_t)http_in_content_length(in));
  {using_buf(&buf) {
    if (buf.base != NULL) {
      buf.base[buf.len] = 0;
      if (buf.len <= 80) {
        printf("body: %zu bytes: %s\n", (size_t)buf.len, buf.base);
      }
      else {
        buf.base[40] = 0;
        printf("body: %zu bytes: %s\n ...\n %s\n", (size_t)buf.len, buf.base, buf.base + buf.len - 40);
      }
    }
  }}
}


void http_req_print() {
  http_in_t* in = http_req();
  printf("%s %s\n headers: \n", http_method_str(http_in_method(in)), http_in_url(in));
  http_in_headers_print(in);
}


void http_in_status_print(http_in_t* in) {
  const char* info = http_in_status_info(in);
  if (info == NULL) info = "";
  printf("status: %i, %s\n", http_in_status(in), info);
  http_in_headers_print(in);
}
