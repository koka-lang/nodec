/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

#pragma once
#ifndef __nodec_internal_h
#define __nodec_internal_h

#include "nodec.h"
#include "nodec-primitive.h"

// ---------------------------------------------------------------------------------
// Private functions used internally to the NodeC library
// todo: is there a better way to define internally only visible routines?
// ---------------------------------------------------------------------------------

lh_value   _channel_async_handler(channel_t* channel, lh_actionfun* action, lh_value arg);
void       _channel_async_req_resume(lh_resume r, lh_value local, uv_req_t* req, uverr_t err);

// These are callback functions to resume requests:
// Calling this will resume the `async_await` call on that request. 
// A call to these will resume at most once! (and be ignored after that)
void       async_req_resume(uv_req_t* uvreq, uverr_t err);
void       async_fs_resume(uv_fs_t* req);

void       nodec_req_force_free(uv_req_t* uvreq);
void       nodec_req_force_freev(lh_value uvreq);

void       nodec_req_free(uv_req_t* uvreq);
void       nodec_req_freev(lh_value uvreq);

void       nodec_owner_release(void* owner);

#define using_req(req_tp,name) \
  req_tp* name = nodec_zero_alloc(req_tp); \
  defer(nodec_req_freev,lh_value_ptr(name))

// A request that is always freed even when canceled. Use this only if
// it is guaranteed that the request is never used again (for example for timers)
#define using_free_req(req_tp,name) \
  req_tp* name = nodec_zero_alloc(req_tp); \
  defer(nodec_req_force_freev,lh_value_ptr(name))


// Await an asynchronous request but return an explicit error value instead of throwing.
// Use with care since these still throw on cancelation requests.
uv_errno_t   asyncx_nocancel_await(uv_req_t* uvreq);  // never throws and cannot be canceled
uv_errno_t   asyncxx_await(uv_req_t* uvreq, uint64_t timeout, void* owner);  // never throws
uv_errno_t   asyncx_await(uv_req_t* req, uint64_t timeout, void* owner);     // throws on cancel
uv_errno_t   asyncx_await_fs(uv_fs_t* req);

// Set a timeout callback 
typedef void uv_timeoutfun(void* arg);
uv_errno_t   _uv_set_timeout(uv_loop_t* loop, uv_timeoutfun* cb, void* arg, uint64_t timeout);
void         nodec_timer_free(uv_timer_t* timer, bool owner_release);

int          channel_receive_nocancel(channel_t* channel, lh_value* data, lh_value* arg);

#define UV_ETHROWCANCEL   (-10000)
#define UV_EHTTP          (-20000)

lh_value async_write_http_exnv(lh_value exnv);

// ---------------------------------------------------------------------------------
// TTY
// ---------------------------------------------------------------------------------

implicit_declare(tty)

lh_value _nodec_tty_allocv();
void     _nodec_tty_freev(lh_value ttyv);
void     async_tty_shutdown();

/// Enable the console in a scope.
/// Inside the given scope, one can use async_tty_readline() and async_tty_write().
///
/// \b Example:
/// ```
/// {using_tty() {
///   async_tty_write("\033[41;37m");         // ANSI escape to set red color
///   async_tty_write("what is your name? ");
///   const char* s = async_tty_readline();
///   {using_free(s) {
///     printf("I got: %s\n", s);
///   }}
/// }}
/// ```
#define using_tty()  \
    using_implicit_defer_exit(async_tty_shutdown(),_nodec_tty_freev,_nodec_tty_allocv(),tty)


// ---------------------------------------------------------------------------------
// Loggin
// ---------------------------------------------------------------------------------

implicit_declare(log)

lh_value _nodec_log_allocv(nodec_log_level_t level);
void     _nodec_log_freev(lh_value logv);
void     nodec_log_shutdown();

#define using_log(level)  \
    using_implicit_defer_exit(nodec_log_shutdown(),_nodec_log_freev,_nodec_log_allocv(level),log)


// ---------------------------------------------------------------------------------
// LibUV streams 
// ---------------------------------------------------------------------------------

// used in http.c for writing errors
uv_errno_t asyncx_uv_write_bufs(uv_stream_t* stream, uv_buf_t* bufs, size_t buf_count);

// used in tty.c
void async_uv_write_buf(uv_stream_t* stream, uv_buf_t buf);
void async_uv_stream_shutdown(uv_stream_t* stream);
void nodec_uv_stream_free(uv_stream_t* stream);

void nodec_tls_stream_handshake(nodec_tls_stream_t* ts);



#endif