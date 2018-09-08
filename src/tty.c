/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "nodec.h"
#include "nodec-internal.h"
#include "nodec-primitive.h"
#include <assert.h>

static uv_stream_t* stream_of_tty(uv_tty_t* tty) {
  return (uv_stream_t*)tty;  // super class
}

typedef struct _tty_t {
  nodec_bstream_t* _stdin;
  uv_tty_t* _stdout;
  uv_tty_t* _stderr;
  int       mode;
} tty_t;

static tty_t* nodec_tty_alloc() {
  return nodec_zero_alloc(tty_t);
}

lh_value _nodec_tty_allocv() {
  return lh_value_ptr(nodec_tty_alloc());
}


static void nodec_tty_free(tty_t* tty) {
  if (tty->_stdin != NULL) nodec_stream_free(as_stream(tty->_stdin));
  if (tty->_stdout != NULL) nodec_uv_stream_free(stream_of_tty(tty->_stdout));
  if (tty->_stderr != NULL) nodec_uv_stream_free(stream_of_tty(tty->_stderr));
  nodec_free(tty);
}


void _nodec_tty_freev(lh_value ttyv) {
  async_tty_shutdown();
  uv_tty_reset_mode();
  nodec_tty_free((tty_t*)lh_ptr_value(ttyv));
}

implicit_define(tty)

static tty_t* tty_get() {
  return (tty_t*)lh_ptr_value(implicit_get(tty));
}

char* async_tty_readline() {
  tty_t* tty = tty_get();
  if (tty->_stdin == NULL) {
    uv_tty_t* hstdin = nodec_zero_alloc(uv_tty_t);
    nodec_check(uv_tty_init(async_loop(), hstdin, 0, 1));
    tty->_stdin = nodec_bstream_alloc_read_ex(stream_of_tty(hstdin), 64, 64);
  }
  return async_read_line(tty->_stdin);
}

void async_tty_write(const char* s) {
  tty_t* tty = tty_get();
  if (tty->_stdout == NULL) {
    tty->_stdout = nodec_zero_alloc(uv_tty_t);
    nodec_check(uv_tty_init(async_loop(), tty->_stdout, 1, 0));
  }
  async_uv_write_buf(stream_of_tty(tty->_stdout), nodec_buf_str(s));
}

void async_tty_vprintf(const char* fmt, va_list args) {
  char buf[512];
  vsnprintf(buf, 512, fmt, args); buf[511] = 0;
  async_tty_write(buf);
}

void async_tty_printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  async_tty_vprintf(fmt, args);
  va_end(args);
}

void async_tty_write_err(const char* s) {
  tty_t* tty = tty_get();
  if (tty->_stderr == NULL) {
    tty->_stderr = nodec_zero_alloc(uv_tty_t);
    nodec_check(uv_tty_init(async_loop(), tty->_stderr, 2, 0));
  }
  async_uv_write_buf(stream_of_tty(tty->_stderr), nodec_buf_str(s));
}

void async_tty_vprintf_err(const char* fmt, va_list args) {
  char buf[512];
  vsnprintf(buf, 512, fmt, args); buf[511] = 0;
  async_tty_write_err(buf);
}

void async_tty_printf_err(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  async_tty_vprintf_err(fmt, args);
  va_end(args);
}

// Flush any outstanding writes
void async_tty_shutdown() {
  tty_t* tty = tty_get();
  if (tty->_stdout == NULL) async_uv_stream_shutdown(stream_of_tty(tty->_stdout));
  if (tty->_stderr == NULL) async_uv_stream_shutdown(stream_of_tty(tty->_stderr));
}

static void wait_for_enter() {
  async_tty_write("press enter to quit...");
  const char* s = async_tty_readline();
  nodec_free(s);
  async_tty_write("canceling...");
}

void async_stop_on_enter(nodec_actionfun_t* action) {
  async_firstof(action, &wait_for_enter);
}