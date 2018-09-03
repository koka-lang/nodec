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


void nodec_stream_free(nodec_stream_t* stream) {
  if (stream->stream_free!=NULL) stream->stream_free(stream);
}

void nodec_stream_freev(lh_value streamv) {
  nodec_stream_free((nodec_stream_t*)lh_ptr_value(streamv));
}

void async_shutdown(nodec_stream_t* stream) {
  if (stream->shutdown != NULL) stream->shutdown(stream);
}

uv_buf_t async_read_bufx(nodec_stream_t* stream, bool* buf_owned) {
  if (stream->read_bufx == NULL) nodec_check_msg(UV_EINVAL, "trying to read from a write stream");
  return stream->read_bufx(stream, buf_owned);
}

uv_buf_t async_read_buf(nodec_stream_t* stream) {
  bool owned = false;
  uv_buf_t buf = async_read_bufx(stream, &owned);
  if (owned || nodec_buf_is_null(buf)) return buf;
  // copy into owned buffer
  uv_buf_t dest = nodec_buf_alloc(buf.len);
  memcpy(dest.base, buf.base, buf.len);
  return dest;
}

void async_write_bufs(nodec_stream_t* stream, uv_buf_t bufs[], size_t count) {
  if (stream->write_bufs == NULL) nodec_check_msg(UV_EINVAL, "trying to write to a read stream");
  stream->write_bufs(stream, bufs, count);
}

void nodec_stream_init(nodec_stream_t* stream,
  async_read_bufx_fun*  read_bufx,
  async_write_bufs_fun* write_bufs,
  async_shutdown_fun*   shutdown,
  nodec_stream_free_fun* stream_free)
{
  stream->read_bufx = read_bufx;
  stream->write_bufs = write_bufs;
  stream->shutdown = shutdown;
  stream->stream_free = stream_free;
}

void nodec_stream_release(nodec_stream_t* stream) {
  // nothing
}


// Return first available data as a string
char* async_read(nodec_stream_t* stream) {
  uv_buf_t buf = async_read_buf(stream);
  if (nodec_buf_is_null(buf)) return NULL;
  assert(buf.base[buf.len] == 0);
  return (char*)buf.base;
}

void async_write_buf(nodec_stream_t* stream, uv_buf_t buf) {
  async_write_bufs(stream, &buf, 1);
}

void async_write(nodec_stream_t* stream, const char* s) {
  uv_buf_t buf = nodec_buf(s, strlen(s));
  async_write_buf(stream, buf);
}


/* ----------------------------------------------------------------------------
Read chunks

read data in a list of buffers (`chunks`) and provide asynchronous functions to read
from these chunks potentially waiting until data is read.
-----------------------------------------------------------------------------*/

static void chunks_init(chunks_t* chunks) {
  memset(chunks, 0, sizeof(*chunks));
}

// push a buffer on the chunks queue
static uv_errno_t chunksx_push(chunks_t* chunks, const uv_buf_t buf, size_t nread) {
  assert(buf.len >= nread);
  if(nread == 0) {
    nodec_buf_free(buf);
    return 0;
  }
  chunk_t* chunk = nodecx_alloc(chunk_t);
  if (chunk == NULL) return UV_ENOMEM;
  // initalize
  chunk->next = NULL;
  chunk->buf = buf;
  // link it 
  if (chunks->last != NULL) {
    chunks->last->next = chunk;
  }
  else {
    chunks->first = chunk;
  }
  chunks->last = chunk;
  chunks->available += nread;
  // ensure buf len is correct
  assert(nread <= chunk->buf.len);
  if (nread < chunk->buf.len) {
    if (chunk->buf.len > 64 && (nread / 4) * 5 <= chunk->buf.len) {
      // more than 64bytes and more than 20% wasted; we reallocate if possible
      void* newbase = nodecx_realloc(chunk->buf.base, nread + 1);
      if (newbase != NULL) chunk->buf.base = newbase;
      chunk->buf.len = (uv_buf_len_t)nread;
    }
    else {
      // just adjust the length and waste some space
      chunk->buf.len = (uv_buf_len_t)nread;
    }
  }
  return 0;
}

static void chunks_push(chunks_t* chunks, const uv_buf_t buf, size_t nread) {
  nodec_check(chunksx_push(chunks, buf, nread));
}

// push a buffer on the head of the chunks queue; buf is taken as is and not resized
static void chunks_push_back(chunks_t* chunks, const uv_buf_t buf) {
  if (nodec_buf_is_null(buf)) return;
  chunk_t* chunk = nodec_alloc(chunk_t);
  // initalize
  chunk->next = chunks->first;
  chunk->buf = buf;
  // link it 
  chunks->first = chunk;
  if (chunks->last == NULL) chunks->last = chunk;
  chunks->available += buf.len;
}

// Free all memory for a chunks queue
static void chunks_release(chunks_t* chunks) {
  chunk_t* chunk = chunks->first;
  while (chunk != NULL) {
    nodec_free(chunk->buf.base);
    chunk_t* next = chunk->next;
    nodec_free(chunk);
    chunk = next;
  }
  chunks->first = chunks->last = NULL;
  chunks->available = 0;
}

// Return the first available buffer in the chunks
uv_buf_t chunks_read_buf(chunks_t* chunks) {
  chunk_t* chunk = chunks->first;
  if (chunk == NULL) {
    assert(chunks->available == 0);
    return nodec_buf_null();
  }
  else {
    uv_buf_t buf = chunk->buf;
    // unlink the first chunk
    chunks->first = chunk->next;
    if (chunks->first == NULL) chunks->last = NULL;
    nodec_free(chunk);
    assert(chunks->available >= buf.len);
    chunks->available -= buf.len;
    assert(chunks->available > 0 || chunks->first == NULL);
    return buf;
  }
}

// Return all available chunks in one buffer
static uv_buf_t chunks_read_buf_all(chunks_t* chunks) {
  chunk_t* chunk = chunks->first;
  if (chunk == NULL) {
    // no available data
    assert(chunks->available == 0);
    return nodec_buf_null();
  }
  else if (chunk->next == NULL) {
    // just one buffer, return it in place
    return chunks_read_buf(chunks);
  }
  else {
    // put all bufs into one larger buf
    uv_buf_t buf = nodec_buf_alloc(chunks->available);
    size_t total = 0;
    size_t nread = 0;
    do {
      uv_buf_t rbuf = chunks_read_buf(chunks);
      nread = rbuf.len;
      assert(nread + total <= buf.len);
      memcpy(buf.base + total, rbuf.base, nread);
      total += nread;
      nodec_buf_free(rbuf);
    } while (nread > 0 && total < buf.len);
    assert(total == buf.len);
    assert(chunks->available == 0);
    assert(chunks->first == NULL);
    return buf;
  }
}


// We can look out for a pattern of at most 8 characters.
// This is used for efficient readline and http header reading.
// Doing it with the 8 last characters remembered makes it much easier 
// to cross chunk boundaries when searching.
typedef struct _find_t {
  const chunk_t* chunk;         // current chunk
  size_t         offset;        // current offset in chunk
  size_t         seen;          // total number of bytes scanned
  uint64_t       last8;         // last 8 characters seen
  uint64_t       pattern;       // the pattern as an 8 character block
  uint64_t       pattern_mask;  // the relevant parts if a pattern is less than 8 characters
} find_t;


static void chunks_find_init(const  chunks_t* chunks, find_t* f, const void* pat, size_t pattern_len) {
  assert(pattern_len <= 8);
  const char* pattern = pat;
  if (pattern_len > 8) lh_throw_str(EINVAL, "pattern too long");
  f->chunk = chunks->first;
  f->offset = 0;
  f->seen = 0;
  f->pattern = 0;
  f->pattern_mask = 0;
  f->last8 = 0;

  // initialize pattern and the mask
  for (size_t i = 0; i < pattern_len; i++) {
    f->pattern = (f->pattern << 8) | pattern[i];
    f->pattern_mask = (f->pattern_mask << 8) | 0xFF;
  }
  // initialize last8 to something different than (any initial part of) pattern
  char c = 0;
  while (memchr(pattern, c, pattern_len) != NULL) c++;
  for (size_t i = 0; i < sizeof(f->last8); i++) {
    f->last8 = (f->last8 << 8) | c;
  }
}

static size_t chunks_find(const chunks_t* chunks, find_t* f) {
  // initialize the initial chunk if needed
  if (f->chunk == NULL) {
    f->chunk = chunks->first;
    if (f->chunk == NULL) return 0;
  }
  // go through all chunks
  do {
    // go through one chunk
    while (f->offset < f->chunk->buf.len) {
      // shift in the new character
      uint8_t b = f->chunk->buf.base[f->offset];
      f->last8 = (f->last8 << 8) | b;
      f->seen++;
      f->offset++;
      // compare against the pattern
      if ((f->last8 & f->pattern_mask) == f->pattern) {
        return f->seen; // found
      }
    }
    // move on to the next chunk if it is there yet
    if (f->chunk->next != NULL) {
      f->chunk = f->chunk->next;
      f->offset = 0;
    }
  } while (f->offset < f->chunk->buf.len);  // done for now
  return 0;
}

/* ----------------------------------------------------------------------------
Buffered streams
Abstract class: buffered streams and basic uv_stream_t derive from this
-----------------------------------------------------------------------------*/

bool async_bstream_read_chunk(nodec_bstream_t* bstream, nodec_chunk_read_t mode, size_t read_to_eof_max) {
  if (bstream->read_chunk == NULL) nodec_check_msg(UV_EINVAL, "trying to read from a write stream");
  return bstream->read_chunk(bstream, mode, read_to_eof_max);
}

void nodec_pushback_buf(nodec_bstream_t* bstream, uv_buf_t buf) {
  if (bstream->pushback_buf == NULL) nodec_check_msg(UV_EINVAL, "trying to read from a write stream");
  bstream->pushback_buf(bstream, buf);
}

void nodec_chunks_pushback_buf(nodec_bstream_t* bstream, uv_buf_t buf) {
  chunks_push_back(&bstream->chunks, buf);
}

void nodec_chunks_push(nodec_bstream_t* bstream, uv_buf_t buf) {
  chunks_push(&bstream->chunks, buf, buf.len);
}

size_t nodec_chunks_available(nodec_bstream_t* bstream) {
  return bstream->chunks.available;
}

uv_buf_t nodec_chunks_read_buf(nodec_bstream_t* bstream) {
  return chunks_read_buf(&bstream->chunks);
}




nodec_stream_t* as_stream(nodec_bstream_t* bstream) {
  return &bstream->stream_t;
}

void nodec_bstream_init(nodec_bstream_t* bstream,
  async_read_chunk_fun* read_chunk,
  nodec_pushback_buf_fun* pushback_buf,
  async_read_bufx_fun*  read_bufx,
  async_write_bufs_fun* write_bufs,
  async_shutdown_fun*   shutdown,
  nodec_stream_free_fun* stream_free)
{
  bstream->source = NULL;
  nodec_stream_init(&bstream->stream_t, read_bufx, write_bufs, shutdown, stream_free);
  bstream->read_chunk = read_chunk;
  bstream->pushback_buf = pushback_buf;
  chunks_init(&bstream->chunks);
}

void nodec_bstream_release(nodec_bstream_t* bstream) {
  chunks_release(&bstream->chunks);
  nodec_stream_release(&bstream->stream_t);
}

// Return a single buffer that contains the entire stream contents
uv_buf_t async_read_buf_all(nodec_bstream_t* bstream, size_t read_max) {
  while (!async_bstream_read_chunk(bstream, CREAD_TO_EOF, read_max)) {
    // wait until eof or read_max
  }
  return chunks_read_buf_all(&bstream->chunks);
}


char* async_read_all(nodec_bstream_t* bstream, size_t read_max) {
  uv_buf_t buf = async_read_buf_all(bstream, read_max);
  if (buf.base == NULL) return NULL;
  assert(buf.base[buf.len] == 0);
  return (char*)buf.base;
}


// Read into a preallocated buffer until the buffer is full or eof.
// Returns the number of bytes read. `at_eof` can be NULL;
//
// Todo: If we drain the current chunks first, we could after that 
// 'allocate' in the given buffer from the callback directly and
// not actually copy the memory but write to it directly.
size_t async_read_into(nodec_bstream_t* bstream, uv_buf_t buf) {
  if (buf.base == NULL || buf.len == 0) return 0;
  size_t total = 0;
  size_t nread = 0;
  do {
    // read available or from source
    bool owned = false;
    uv_buf_t rbuf = async_read_bufx(&bstream->stream_t,&owned);
    {using_buf_owned(owned, &rbuf) {
      nread = rbuf.len;
      if (nread > 0) {
        // copy into buffer
        size_t ncopy = (total + nread > buf.len ? buf.len - total : nread);
        memcpy(buf.base + total, rbuf.base, ncopy);
        total += ncopy;
        if (ncopy < nread) {
          // push back the bytes that didn't fit
          if (owned) {
            // note: we don't reallocate since chunks are short-lived anyway
            memmove(rbuf.base, rbuf.base + ncopy, rbuf.len - ncopy);
            rbuf.len -= (uv_buf_len_t)ncopy;
          }
          else {
            uv_buf_t dest = nodec_buf_alloc(rbuf.len - ncopy);
            memcpy(dest.base, rbuf.base + ncopy, dest.len);
            rbuf = dest; // overwrite, as we don't own rbuf anyway
          }
          nodec_pushback_buf(bstream, rbuf);
          rbuf = nodec_buf_null(); // null out `rbuf` so it does not get freed          
        }
      }
    }}
  } while (nread > 0 && total < buf.len);
  return total;
}



// --------------------------------------------------------------------
// Reading up to a pattern

static size_t async_bstream_find(nodec_bstream_t* bstream, const void* pat, size_t pat_len, size_t read_max) {
  find_t find;
  chunks_find_init(&bstream->chunks, &find, pat, pat_len);
  size_t toread = 0;
  bool eof = false;
  while (!eof && (toread = chunks_find(&bstream->chunks, &find)) == 0 && (read_max == 0 || nodec_chunks_available(bstream) < read_max)) {
    eof = async_bstream_read_chunk(bstream, CREAD_EVEN_IF_AVAILABLE, 0); // read direct and push into the chunks
  }
  return toread;
}

uv_buf_t async_read_buf_including(nodec_bstream_t* bstream, size_t* toread, const void* pat, size_t pat_len, size_t read_max) {
  if (toread != NULL) *toread = 0;
  size_t n = async_bstream_find(bstream, pat, pat_len, read_max);
  if (toread != NULL) *toread = n;
  if (n == 0) return nodec_buf_null();
  return chunks_read_buf_all(&bstream->chunks);
}

uv_buf_t async_read_buf_upto(nodec_bstream_t* bstream, const void* pat, size_t pat_len, size_t read_max) {
  size_t toread = 0;
  uv_buf_t buf = async_read_buf_including(bstream, &toread, pat, pat_len, read_max);
  if (nodec_buf_is_null(buf) || toread == buf.len) return buf;
  assert(toread < buf.len);
  size_t n = buf.len - toread;
  uv_buf_t xbuf = nodec_buf_alloc(n);
  memcpy(xbuf.base, buf.base + toread, n);
  nodec_pushback_buf(bstream, xbuf);
  return nodec_buf_fit(buf, toread);
}

uv_buf_t async_read_buf_line(nodec_bstream_t* bstream) {
  return async_read_buf_upto(bstream, "\n", 1, 64*NODEC_KB);
}

char* async_read_line(nodec_bstream_t* bstream) {
  uv_buf_t buf = async_read_buf_line(bstream);
  if (buf.base == NULL) return NULL;
  assert(buf.base[buf.len] == 0);
  return (char*)buf.base;
}


/* ----------------------------------------------------------------------------
  Some explicit error-returning functions
-----------------------------------------------------------------------------*/

typedef struct _write_buf_args_t {
  nodec_stream_t* s;
  uv_buf_t        buf;
} write_buf_args_t;

static lh_value _asyncx_write_buf(lh_value argsv) {
  write_buf_args_t* args = (write_buf_args_t*)lh_ptr_value(argsv);
  async_write_buf(args->s, args->buf);
  return lh_value_null;
}

uv_errno_t asyncx_write_buf(nodec_stream_t* s, uv_buf_t buf) {
  lh_exception* exn = NULL;
  write_buf_args_t args = { s, buf };
  lh_try(&exn, &_asyncx_write_buf, lh_value_any_ptr(&args));
  return (exn != NULL ? exn->code : 0);
}


typedef struct _read_buf_args_t {
  nodec_bstream_t* s;
  uv_buf_t         buf;
} read_buf_args_t;

static lh_value _asyncx_read_into(lh_value argsv) {
  read_buf_args_t* args = (read_buf_args_t*)lh_ptr_value(argsv);
  return lh_value_long((long)(async_read_into(args->s, args->buf)));
}

uv_errno_t asyncx_read_into(nodec_bstream_t* s, uv_buf_t buf, size_t* nread) {
  if (nread != NULL) *nread = 0;
  lh_exception* exn = NULL;
  read_buf_args_t args = { s, buf };
  lh_value nreadv = lh_try(&exn, &_asyncx_read_into, lh_value_any_ptr(&args));
  if (exn != NULL) return exn->code;
  if (nread != NULL) *nread = (size_t)lh_long_value(nreadv);
  return 0;
}


/* ----------------------------------------------------------------------------
Buffer any stream
-----------------------------------------------------------------------------*/

static uv_buf_t async_bufstream_read_bufx(nodec_stream_t* stream, bool* owned) {
nodec_bstream_t* bs = (nodec_bstream_t*)stream;
  if (nodec_chunks_available(bs) > 0) {
    if (owned != NULL) *owned = true;
    return chunks_read_buf(&bs->chunks);
  }
  else {
    return async_read_bufx(bs->source, owned);
  }
}

static void async_bufstream_write_bufs(nodec_stream_t* stream, uv_buf_t bufs[], size_t count) {
  nodec_bstream_t* bs = (nodec_bstream_t*)stream;
  async_write_bufs(bs->source, bufs, count);
}

static bool async_bufstream_read_chunk(nodec_bstream_t* bs, nodec_chunk_read_t read_mode, size_t read_to_eof_max) {
  if (read_mode == CREAD_NORMAL && nodec_chunks_available(bs) > 0) {
    return false;
  }
  else {
    uv_buf_t buf;
    if (read_mode != CREAD_TO_EOF) read_to_eof_max = 0;
    do {
      buf = async_read_buf(bs->source);
      chunks_push(&bs->chunks, buf, buf.len);
    } while (!nodec_buf_is_null(buf) && nodec_chunks_available(bs) < read_to_eof_max);
    return (nodec_buf_is_null(buf));
  }
}

static void async_bufstream_shutdown(nodec_stream_t* stream) {
  nodec_bstream_t* bs = (nodec_bstream_t*)stream;
  async_shutdown(bs->source);
}

static void async_bufstream_free(nodec_stream_t* stream) {
  nodec_bstream_t* bs = (nodec_bstream_t*)stream;
  nodec_stream_free(bs->source);
  nodec_bstream_release(bs);
  nodec_free(bs);
}

nodec_bstream_t* nodec_bstream_alloc_on(nodec_stream_t* source) {
  nodec_bstream_t* bs = nodec_zero_alloc(nodec_bstream_t);
  nodec_bstream_init(bs,
    &async_bufstream_read_chunk, &nodec_chunks_pushback_buf,
    &async_bufstream_read_bufx, &async_bufstream_write_bufs,
    &async_bufstream_shutdown, &async_bufstream_free);
  bs->source = source;
  return bs;
}




/* ----------------------------------------------------------------------------
Await shutdown
-----------------------------------------------------------------------------*/

static void async_await_shutdown(uv_shutdown_t* req, uv_stream_t* stream) {
  async_await_owned((uv_req_t*)req, stream);
}

static void async_shutdown_resume(uv_shutdown_t* req, uverr_t status) {
  async_req_resume((uv_req_t*)req, status);
}


/* ----------------------------------------------------------------------------
Await write requests
-----------------------------------------------------------------------------*/

/*
static void async_await_write(uv_write_t* req, uv_stream_t* owner) {
  async_await_owned((uv_req_t*)req, owner);
}
*/

static uv_errno_t asyncx_await_write(uv_write_t* req, uv_stream_t* owner) {
  return asyncx_await_owned((uv_req_t*)req, owner);
}


static void async_write_resume(uv_write_t* req, uverr_t status) {
  async_req_resume((uv_req_t*)req, status);
}

uv_errno_t asyncx_uv_write_bufs(uv_stream_t* stream, uv_buf_t* bufs, size_t buf_count) {
  if (bufs == NULL || buf_count <= 0) return 0;
  uv_errno_t err = 0;
  {using_req(uv_write_t, req) {
    // Todo: verify it is ok to have bufs on the stack or if we need to heap alloc them first for safety
    err = uv_write(req, stream, bufs, (unsigned)buf_count, &async_write_resume);
    if (err == 0) err = asyncx_await_write(req, stream);
  }}
  return err;
}

static uv_errno_t asyncx_uv_write_buf(uv_stream_t* stream, uv_buf_t buf) {
  return asyncx_uv_write_bufs(stream, &buf, 1);
}

static void async_uv_write_bufs(uv_stream_t* stream, uv_buf_t bufs[], size_t buf_count) {
  nodec_check_data(asyncx_uv_write_bufs(stream, bufs, buf_count),stream);
}

void async_uv_write_buf(uv_stream_t* stream, uv_buf_t buf) {
  nodec_check_data(asyncx_uv_write_buf(stream, buf),stream);
}

void async_uv_stream_shutdown(uv_stream_t* stream) {
  if (stream == NULL) return;
  if (stream->write_queue_size > 0) {
    {using_req(uv_shutdown_t, req) {
      nodec_check(uv_shutdown(req, stream, &async_shutdown_resume));
      async_await_shutdown(req, stream);
    }}
  }
}


struct _nodec_uv_stream_t {
  nodec_bstream_t bstream_t;     // a buffered stream; with source=NULL
  uv_stream_t*    stream;        // backlink, when reading stream->data == this
  size_t          alloc_size;    // current chunk allocation size (<= alloc_max), doubled on every new read
  size_t          alloc_max;     // maximal chunk allocation size (usually about 64kb)
  size_t          read_to_eof_max;   // set > 0 to improve perfomance for reading a stream up to eof
  uv_req_t*       req;           // request object for waiting
  volatile size_t     read_total;    // total bytes read until now (available <= total)
  volatile bool       eof;           // true if end-of-file reached
  volatile uv_errno_t err;           // !=0 on error
};


nodec_bstream_t* as_bstream(nodec_uv_stream_t* stream) {
  return &stream->bstream_t;
}


static void nodecx_uv_stream_push(nodec_uv_stream_t* rs, const uv_buf_t buf, size_t nread) {
  if (nread == 0 || buf.base == NULL) return;
  rs->err = chunksx_push(&rs->bstream_t.chunks, buf, nread);    
  if (rs->err != 0) {
    nodec_free(buf.base);
  }
  else {
    rs->read_total += nread;
    if (rs->read_to_eof_max > 0) {
      rs->read_to_eof_max = (rs->read_to_eof_max < nread ? 0 : rs->read_to_eof_max - nread);      
    }
  }  
}

static void nodec_uv_stream_try_resume(nodec_uv_stream_t* rs) {
  assert(rs != NULL);
  uv_req_t* req = rs->req;
  if (req == NULL) return;
  async_req_resume(req, rs->err);
}

/*
static void nodec_uv_stream_try_resumev(void* rsv) {
  nodec_uv_stream_try_resume((nodec_uv_stream_t*)rsv);
}
*/

static void nodec_uv_stream_freereq(nodec_uv_stream_t* rs) {
  if (rs != NULL && rs->req != NULL) {
    nodec_req_free(rs->req);  // on explicit cancelation, don't immediately free
    rs->req = NULL;
  }
}

static void nodec_uv_stream_freereqv(lh_value rsv) {
  nodec_uv_stream_freereq(lh_ptr_value(rsv));
}

static  uverr_t asyncx_uv_stream_await(nodec_uv_stream_t* rs, bool wait_even_if_available, uint64_t timeout) {
  if (rs == NULL) return UV_EINVAL;
  if ((wait_even_if_available || nodec_chunks_available(&rs->bstream_t) == 0) && rs->err == 0 && !rs->eof) {
    // await an event
    if (rs->req != NULL) lh_throw_str(UV_EINVAL, "only one strand can await a read stream");
    uv_req_t* req = nodec_zero_alloc(uv_req_t);
    rs->req = req;
    {defer(nodec_uv_stream_freereqv, lh_value_ptr(rs)) {
      rs->err = asyncx_await(req, timeout, rs->stream);
      //async_await_owned(req, rs->stream);
      // fprintf(stderr,"back from await\n");
    }}
  }
  if (rs->eof) return UV_EOF;
  return rs->err;
}

static bool async_uv_stream_await(nodec_uv_stream_t* rs, bool wait_even_if_available) {
  if (rs == NULL) return true;
  uverr_t err = asyncx_uv_stream_await(rs, wait_even_if_available, 0);
  if (err != UV_EOF) nodec_check(err);
  return (err == UV_EOF);
}

uv_errno_t asyncx_uv_stream_await_available(nodec_uv_stream_t* stream, int64_t timeout) {
  return asyncx_uv_stream_await(stream, false, timeout);
}


static void nodec_uv_stream_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  if (handle == NULL) return;
  nodec_uv_stream_t* rs = (nodec_uv_stream_t*)handle->data;
  if (rs == NULL) return;
  // allocate
  size_t len = (rs->alloc_size > 0 ? rs->alloc_size : suggested_size);
  buf->base = nodecx_malloc(len + 1);  // always allow a zero at the end
  buf->len  = (buf->base == NULL ? 0 : (uv_buf_len_t)len);
  // increase allocation size
  if (rs->alloc_size > 0 && rs->alloc_size < rs->alloc_max) {
    size_t newsize = 2 * rs->alloc_size;
    rs->alloc_size = (newsize > rs->alloc_max || newsize < rs->alloc_size ? rs->alloc_max : newsize);
  }
}


static void _nodec_uv_stream_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  nodec_uv_stream_t* rs = (nodec_uv_stream_t*)stream->data;
  if (nread <= 0 || rs == NULL) {
    if (buf != NULL && buf->base != NULL) {
      nodec_free(buf->base);
      //buf->base = NULL;
    }
  }

  if (rs != NULL) {
    if (nread > 0) {
      // always terminate with zero
      assert(buf->len >= nread);
      buf->base[nread] = 0;
      // data available
      nodecx_uv_stream_push(rs, *buf, (size_t)nread);
      if (rs->read_to_eof_max == 0 || rs->eof) {
        nodec_uv_stream_try_resume(rs);
      }
    }
    else if (nread < 0) {
      // done reading (error or UV_EOF)
      if (nread == UV_ECANCELED) nread = UV_EOF;
      rs->err = (uv_errno_t)(nread == UV_EOF ? 0 : nread);
      if (nread == UV_EOF) {
        rs->eof = true;
      }
      uv_read_stop(stream);       // no more reading
      nodec_uv_stream_try_resume(rs);
    }
    else {
      // E_AGAIN or E_WOULDBLOCK (but not EOF)
      // Todo: just ignore? or try resume?
    }
  }
  return;
}

void nodec_uv_stream_read_restart(nodec_uv_stream_t* rs) {
  uv_errno_t err = uv_read_start(rs->stream, &nodec_uv_stream_alloc_cb, &_nodec_uv_stream_cb);
  if (err != 0 && err != UV_EALREADY) nodec_check(err);
}

void nodec_uv_stream_read_start(nodec_uv_stream_t* rs, size_t alloc_init, size_t alloc_max) {
  if (rs->stream->data == NULL) {
    rs->stream->data = rs;
  }
  assert(rs != NULL && rs->stream->data == rs);
  rs->read_to_eof_max = 0;
  //rs->read_max = (read_max > 0 ? read_max : 1024 * 1024 * 1024);  // 1Gb by default
  rs->alloc_size = (alloc_init == 0 ? 1024 : alloc_init);  // start small but double at every new read
  rs->alloc_max = (alloc_max == 0 ? 64 * 1024 : alloc_max);
  nodec_uv_stream_read_restart(rs);
}


void nodec_uv_stream_read_stop(nodec_uv_stream_t* rs) {
  if (rs->stream->data == NULL) return;
  nodec_check(uv_read_stop(rs->stream));
}


static void close_handle_cb(uv_handle_t* h) {
  /*
  if ((h->type == UV_STREAM || h->type == UV_TCP || h->type == UV_TTY) && h->data != NULL) {
  read_stream_free((read_stream_t*)h->data);
  }
  */
  nodec_free(h);
}

void nodec_handle_free(uv_handle_t* h) {
  // Todo: this is philosophically "wrong" as the callback is called outside of
  // our framework (i.e. we should do an await). 
  // but we're ok since the callback does just a free.
  if (h != NULL && !uv_is_closing(h)) {
    uv_close(h, &close_handle_cb);
  }
  else {
    close_handle_cb(h);
  }
  nodec_owner_release(h);
}

void nodec_uv_stream_free(uv_stream_t* stream) {
  if (stream->data != NULL) {
    // read stream
    uv_read_stop(stream);
    stream->data = NULL;
  }
  nodec_handle_free((uv_handle_t*)stream);
}

void nodec_uv_stream_freev(lh_value uvstreamv) {
  nodec_uv_stream_free((uv_stream_t*)lh_ptr_value(uvstreamv));
}
static uv_buf_t async_uv_stream_read_bufx(nodec_stream_t* s, bool* owned) {
  nodec_uv_stream_t* rs = (nodec_uv_stream_t*)s;
  if (owned != NULL) *owned = true;
  async_uv_stream_await(rs, false);
  assert(rs->eof || nodec_chunks_available(&rs->bstream_t) > 0);
  return chunks_read_buf(&rs->bstream_t.chunks);
}

static bool async_uv_stream_read_chunk(nodec_bstream_t* s, nodec_chunk_read_t read_mode, size_t read_to_eof_max) {
  nodec_uv_stream_t* rs = (nodec_uv_stream_t*)s;
  if (read_mode == CREAD_TO_EOF) {
    rs->read_to_eof_max = read_to_eof_max; // reduces resumes
    while (!async_uv_stream_await(rs, true)) {
      // wait until eof
    }
    rs->read_to_eof_max = 0; 
  }
  else {
    async_uv_stream_await(rs, (read_mode == CREAD_EVEN_IF_AVAILABLE));
  }
  return rs->eof;
}

static void async_uv_stream_write_bufsx(nodec_stream_t* s, uv_buf_t bufs[], size_t buf_count) {
  nodec_uv_stream_t* rs = (nodec_uv_stream_t*)s;
  async_uv_write_bufs(rs->stream, bufs, buf_count);
}


static void async_uv_stream_shutdownx(nodec_stream_t* stream) {
  nodec_uv_stream_t* rs = (nodec_uv_stream_t*)stream;
  async_uv_stream_shutdown(rs->stream);
}


static void nodec_uv_stream_freex(nodec_stream_t* stream) {
  nodec_uv_stream_t* rs = (nodec_uv_stream_t*)stream;
  nodec_uv_stream_free(rs->stream);
  rs->stream = NULL;
  nodec_bstream_release(&rs->bstream_t);
  nodec_free(rs);
}


nodec_uv_stream_t* nodec_uv_stream_alloc(uv_stream_t* stream) {
  nodec_uv_stream_t* rs = NULL;
  {on_abort(nodec_uv_stream_freev, lh_value_any_ptr(stream)) {
    rs = nodec_zero_alloc(nodec_uv_stream_t);
    nodec_bstream_init(&rs->bstream_t,
      &async_uv_stream_read_chunk, &nodec_chunks_pushback_buf,
      &async_uv_stream_read_bufx, &async_uv_stream_write_bufsx,
      &async_uv_stream_shutdownx, &nodec_uv_stream_freex);
    rs->stream = stream;
  }}
  return rs;
}

nodec_bstream_t* nodec_bstream_alloc(uv_stream_t* stream) {
  nodec_uv_stream_t* rs = nodec_uv_stream_alloc(stream);
  return &rs->bstream_t;
}

nodec_bstream_t* nodec_bstream_alloc_read_ex(uv_stream_t* stream, size_t alloc_init, size_t alloc_max) {
  nodec_uv_stream_t* rs = nodec_uv_stream_alloc(stream);
  nodec_uv_stream_read_start(rs, alloc_init, alloc_max);
  return &rs->bstream_t;
}

nodec_bstream_t* nodec_bstream_alloc_read(uv_stream_t* stream) {
  return nodec_bstream_alloc_read_ex(stream, 0, 0);
}


/* ----------------------------------------------------------------------------
  (G)Zip streams
-----------------------------------------------------------------------------*/

#ifdef USE_ZLIB

#include <zlib/zlib.h>

typedef struct _nodec_zstream_t {
  nodec_bstream_t bstream;
  z_stream        read_strm;
  z_stream        write_strm;
  uv_buf_t        write_buf;
  nodec_stream_t* source;
  size_t          chunk_size;
  size_t          nread;
  size_t          nwritten;
  int             compress_level;
  bool            gzip;
} nodec_zstream_t;

static void* nodec_zalloc(void* opaque, unsigned size, unsigned count) {
  return nodecx_calloc(size, count);
}

static void nodec_zfree(void* opaque, void* p) {
  nodec_free(p);
}

static void nodec_zstream_init_read(nodec_zstream_t* zs) {
  if (zs->read_strm.zalloc == NULL) {
    zs->read_strm.zalloc = &nodec_zalloc;
    zs->read_strm.zfree = &nodec_zfree;
    zs->read_strm.opaque = Z_NULL;
    zs->read_strm.avail_in = 0;
    zs->read_strm.next_in = Z_NULL;
    int res = inflateInit2(&zs->read_strm, 15 + (zs->gzip ? 32 : 0)); // read either gzip or zlib
    if (res != 0) nodec_check_msg(UV_ENOMEM, "cannot initialize inflate");
  }
}

static void nodec_zstream_init_write(nodec_zstream_t* zs) {
  if (zs->write_strm.zalloc == NULL) {
    zs->write_strm.zalloc = &nodec_zalloc;
    zs->write_strm.zfree = &nodec_zfree;
    zs->write_strm.opaque = Z_NULL;
    zs->write_strm.avail_in = 0;
    zs->write_strm.next_in = Z_NULL;
    int res = deflateInit2(&zs->write_strm, zs->compress_level, Z_DEFLATED, 15 + (zs->gzip ? 16 : 0), 8, Z_DEFAULT_STRATEGY);
    if (res != 0) nodec_check_msg(UV_ENOMEM, "cannot initialize deflate");
    zs->write_buf = nodec_buf_alloc(zs->chunk_size);
  }
}

static bool async_zstream_read_chunks(nodec_zstream_t* zs) 
{
  nodec_zstream_init_read(zs);
  bool owned = false;
  uv_buf_t src = async_read_bufx(zs->source, &owned);
  if (nodec_buf_is_null(src)) return true;  // eof
  {using_buf_owned(owned, &src) {
    zs->read_strm.avail_in = src.len;
    zs->read_strm.next_in = (void*)src.base;
    zs->nread += src.len;
    // decompress while data available into chunks
    int res = 0;
    do {
      uv_buf_t dest = nodec_buf_alloc(zs->chunk_size);
      zs->read_strm.avail_out = dest.len;
      zs->read_strm.next_out = (void*)dest.base;
      res = inflate(&zs->read_strm, Z_NO_FLUSH);
      if (res >= Z_OK) {
        size_t nwritten = dest.len - zs->read_strm.avail_out;
        chunks_push(&zs->bstream.chunks, dest, nwritten);
      }
      else {
        nodec_check_msg(UV_EINVAL, "invalid gzip data");
      }
    } while (res >= Z_OK && zs->read_strm.avail_out == 0);  // while fully written buffers
  }}
  return false;
}

static uv_buf_t async_zstream_read_bufx(nodec_stream_t* stream, bool* owned) {
  nodec_zstream_t* zs = (nodec_zstream_t*)stream;
  if (owned != NULL) *owned = true;
  if (nodec_chunks_available(&zs->bstream) == 0) {
    async_zstream_read_chunks(zs);
  }
  // hopefully by now we have available chunks
  return nodec_chunks_read_buf(&zs->bstream);
}

static bool async_zstream_read_chunk(nodec_bstream_t* s, nodec_chunk_read_t read_mode, size_t read_to_eof_max) {
  nodec_zstream_t* zs = (nodec_zstream_t*)s;
  if (read_mode == CREAD_NORMAL && nodec_chunks_available(&zs->bstream) > 0) {
    return false;
  }
  else {
    bool eof = false;
    if (read_mode != CREAD_TO_EOF) read_to_eof_max = 0;
    do {
      eof = async_zstream_read_chunks(zs);
    } while (!eof && nodec_chunks_available(&zs->bstream) < read_to_eof_max);
    return eof;
  }
}

static void async_zstream_write_buf(nodec_zstream_t* zs, uv_buf_t src, int flush) {
  nodec_zstream_init_write(zs);
  zs->nwritten += src.len;
  zs->write_strm.avail_in = src.len;
  zs->write_strm.next_in = (void*)src.base;
  int res = 0;
  do {
    zs->write_strm.avail_out = zs->write_buf.len;
    zs->write_strm.next_out = (void*)zs->write_buf.base;
    res = deflate(&zs->write_strm, flush);
    if (res >= Z_OK) {  // Z_OK || Z_STREAM_END
      size_t nwritten = zs->write_buf.len - zs->write_strm.avail_out;
      if (nwritten > 0) {
        async_write_buf(zs->source, nodec_buf(zs->write_buf.base, nwritten));
      }
    }
  } while (res >= Z_OK && zs->write_strm.avail_out == 0);  // while full buffers
}

static void async_zstream_write_bufs(nodec_stream_t* s, uv_buf_t bufs[], size_t buf_count) {
  nodec_zstream_t* zs = (nodec_zstream_t*)s;
  for (size_t i = 0; i < buf_count; i++) {
    uv_buf_t src = bufs[i];
    if (nodec_buf_is_null(src)) continue;
    async_zstream_write_buf(zs, src, Z_NO_FLUSH);
  }
}

static void async_zstream_shutdown(nodec_stream_t* stream) {
  nodec_zstream_t* zs = (nodec_zstream_t*)stream;
  if (zs->nwritten > 0) {
    async_zstream_write_buf(zs, nodec_buf_null(), Z_FINISH);
  }
  async_shutdown(zs->source);
}

static void nodec_zstream_free(nodec_stream_t* stream) {
  nodec_zstream_t* zs = (nodec_zstream_t*)stream;
  if (zs->read_strm.zalloc != NULL) inflateEnd(&zs->read_strm);
  if (zs->write_strm.zalloc != NULL) deflateEnd(&zs->write_strm);
  nodec_buf_free(zs->write_buf);
  nodec_stream_free(zs->source);
  nodec_free(zs);
}


nodec_bstream_t* nodec_zstream_alloc_ex(nodec_stream_t* stream, int compress_level, bool gzip) {
  int res = 0;
  nodec_zstream_t* zs = nodecx_alloc(nodec_zstream_t);
  if (zs == NULL) { res = UV_ENOMEM; goto err; }
  zs->write_buf = nodec_buf_null();
  zs->chunk_size = 64 * 1024;       // (de)compress in 64kb chunks
  zs->nread = 0;
  zs->nwritten = 0;
  zs->source = stream;
  zs->gzip = gzip;
  zs->compress_level = compress_level;
  nodec_bstream_init(&zs->bstream,
    &async_zstream_read_chunk, &nodec_chunks_pushback_buf,
    &async_zstream_read_bufx, &async_zstream_write_bufs,
    &async_zstream_shutdown, &nodec_zstream_free);
  memset(&zs->read_strm, 0, sizeof(z_stream));
  memset(&zs->write_strm, 0, sizeof(z_stream));  
  return &zs->bstream;
err:
  if (zs != NULL) nodec_free(zs);
  if (stream != NULL) nodec_stream_free(stream);
  nodec_check(res);
  return NULL;
}

nodec_bstream_t* nodec_zstream_alloc(nodec_stream_t* stream) {
  return nodec_zstream_alloc_ex(stream, 6, true);
}



#endif // NO_ZLIB


