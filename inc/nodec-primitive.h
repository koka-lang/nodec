/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

#pragma once
#ifndef __nodec_primitive_h
#define __nodec_primitive_h

#include "nodec.h"

// ---------------------------------------------------------------------------------
// NodeC primitive functions that might be used by certain clients
// but are not generally exposed.
// ---------------------------------------------------------------------------------

typedef int uverr_t;

// Check the an error value and throw if it is not zero.
void nodec_check(uverr_t err);
void nodec_check_msg(uverr_t err, const char* msg);

#if (defined(_WIN32) || defined(_WIN64))
typedef ULONG  uv_buf_len_t;
#else
typedef size_t uv_buf_len_t;
#endif



/* ----------------------------------------------------------------------------
Asynchronous primitives
-----------------------------------------------------------------------------*/

// Return the current event loop (ambiently bound by the async handler)
uv_loop_t* async_loop();

// Await an asynchronous request. Throws on error. 
// If canceled, the request is deallocated when the original callback is invoked.
// This is used for 'one of' callbacks, like `fs_stat`.
void       async_await_once(uv_req_t* req);

uv_errno_t asyncx_await_once(uv_req_t* uvreq);
uv_errno_t asyncx_await_owned(uv_req_t* uvreq, void* owner);

// Await an asynchronous request. 
// If canceled, the request is deallocated when the `owner` (usually a `uv_handle_t*`)
// is released. This is used for streams or timers.
void       async_await_owned(uv_req_t* req, void* owner);



/* ----------------------------------------------------------------------------
Channels
-----------------------------------------------------------------------------*/
typedef void (channel_release_elem_fun)(lh_value data, lh_value arg, int err);

channel_t*    channel_alloc(ssize_t queue_max);
channel_t*    channel_alloc_ex(ssize_t queue_max, lh_releasefun* release, lh_value release_arg, channel_release_elem_fun* release_elem);
void          channel_free(channel_t* channel);
void          channel_freev(lh_value vchannel);
#define using_channel(name) channel_t* name = channel_alloc(-1); defer(&channel_freev,lh_value_ptr(name))

uv_errno_t    channel_emit(channel_t* channel, lh_value data, lh_value arg, int err);
int           channel_receive(channel_t* channel, lh_value* data, lh_value* arg);
bool          channel_is_full(channel_t* channel);


// ---------------------------------------------------------------------------------
// Internal stream functions
// ---------------------------------------------------------------------------------

typedef void     (nodec_stream_free_fun)(nodec_stream_t* stream);
typedef void     (async_shutdown_fun)(nodec_stream_t* stream);
typedef uv_buf_t(async_read_buf_fun)(nodec_stream_t* stream);
typedef void     (async_write_bufs_fun)(nodec_stream_t* stream, uv_buf_t bufs[], size_t count);

struct _nodec_stream_t {
  async_read_buf_fun*     read_buf;
  async_write_bufs_fun*   write_bufs;
  async_shutdown_fun*     shutdown;
  nodec_stream_free_fun*  stream_free;
};

void nodec_stream_init(nodec_stream_t* stream,
  async_read_buf_fun*  read_buf,
  async_write_bufs_fun* write_bufs,
  async_shutdown_fun*   shutdown,
  nodec_stream_free_fun* stream_free);
void nodec_stream_release(nodec_stream_t* stream);

typedef struct _chunk_t {
  struct _chunk_t* next;
  uv_buf_t     buf;
} chunk_t;

typedef struct _chunks_t {
  chunk_t*     first;
  chunk_t*     last;
  size_t       available;
} chunks_t;

typedef enum _nodec_chunk_read_t {
  CREAD_NORMAL,
  CREAD_EVEN_IF_AVAILABLE,
  CREAD_TO_EOF
} nodec_chunk_read_t;

typedef bool (async_read_chunk_fun)(nodec_bstream_t* bstream, nodec_chunk_read_t mode);

struct _nodec_bstream_t {
  nodec_stream_t        stream_t;
  async_read_chunk_fun* read_chunk;
  chunks_t              chunks;
  nodec_stream_t*       source;
};

void nodec_bstream_init(nodec_bstream_t* bstream,
  async_read_chunk_fun* read_chunk,
  async_read_buf_fun*  read_buf,
  async_write_bufs_fun* write_bufs,
  async_shutdown_fun*   shutdown,
  nodec_stream_free_fun* stream_free);

void nodec_bstream_release(nodec_bstream_t* bstream);

// ---------------------------------------------------------------------------------
// LibUV streams 
// ---------------------------------------------------------------------------------


uv_errno_t asyncx_uv_write_bufs(uv_stream_t* stream, uv_buf_t bufs[], size_t buf_count);
uv_errno_t asyncx_uv_write_buf(uv_stream_t* stream, uv_buf_t buf);
void async_uv_write_bufs(uv_stream_t* stream, uv_buf_t bufs[], size_t buf_count);
void async_uv_write_buf(uv_stream_t* stream, uv_buf_t buf);
void async_uv_stream_shutdown(uv_stream_t* stream);
void nodec_uv_stream_free(uv_stream_t* stream);


typedef struct _nodec_uv_stream_t nodec_uv_stream_t;
nodec_bstream_t* as_bstream(nodec_uv_stream_t* stream);

#define using_uv_stream(s) using_bstream(as_bstream(s))

nodec_uv_stream_t* nodec_uv_stream_alloc(uv_stream_t* stream);
void nodec_uv_stream_read_start(nodec_uv_stream_t* rs, size_t read_max, size_t alloc_init, size_t alloc_max);
void nodec_uv_stream_read_restart(nodec_uv_stream_t* rs);
void nodec_uv_stream_set_read_max(nodec_uv_stream_t* rs, size_t read_max);
void nodec_uv_stream_read_stop(nodec_uv_stream_t* rs);

// Used to implement keep-alive
uv_errno_t asyncx_uv_stream_await_available(nodec_uv_stream_t* stream, int64_t timeout);



#endif