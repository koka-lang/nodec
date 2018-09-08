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

typedef struct _log_t {
  bool              no_console;
  nodec_log_level_t level;
  uv_file           file;
} log_t;

static log_t* nodec_log_alloc(nodec_log_level_t level) {
  log_t* log = nodec_zero_alloc(log_t);
  log->level = level;
  return log;
}

lh_value _nodec_log_allocv(nodec_log_level_t level) {
  return lh_value_ptr(nodec_log_alloc(level));
}


static void nodec_log_free(log_t* log) {
  //if (log->file != 0) async_fs_close(log->file);
  nodec_free(log);
}

void _nodec_log_freev(lh_value logv) {
  nodec_log_free((log_t*)lh_ptr_value(logv));
}

implicit_define(log)

static log_t* log_get() {
  return (log_t*)lh_ptr_value(implicit_get(log));
}

void nodec_log_shutdown() {
  log_t* log = log_get();
  if (log->file > 0) {
    async_fs_close(log->file);
    log->file = -1;
  }
}

static const char* nodec_level_str(nodec_log_level_t level) {
  static const char* level_strs[6] = {
    "error",
    "warning",
    "info",
    "verbose",
    "debug",
    "silly"
  };
  if (level < 0 || level >= 6) level = 5;
  return level_strs[level];
}

void nodec_log(nodec_log_level_t level, const char* msg) {
  log_t* log = log_get();
  if (log->level < level) return;
  if (!log->no_console) {
    const char* levelmsg = nodec_level_str(level);
    if (log->level <= LOG_WARNING) 
      async_tty_printf_err("%s: %s\n", levelmsg, msg);
    else 
      async_tty_printf("%s: %s\n", levelmsg, msg);
  }
  // TODO: file logging
}

void nodec_log_set(nodec_log_level_t level) {
  log_t* log = log_get();
  if (level < LOG_ERROR) level = LOG_ERROR;
  log->level = level;
}

void nodec_vlogf(nodec_log_level_t level, const char* fmt, va_list args) {
  log_t* log = log_get();
  if (log->level >= level) {
    char buf[512];
    vsnprintf(buf, 512, fmt, args); buf[511] = 0;
    nodec_log(level, buf);
  }
}

void nodec_logf(nodec_log_level_t level, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  nodec_vlogf(level, fmt, args);
  va_end(args);
}

void nodec_log_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  nodec_vlogf(LOG_ERROR, fmt, args);
  va_end(args);
}

void nodec_log_warning(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  nodec_vlogf(LOG_WARNING, fmt, args);
  va_end(args);
}

void nodec_log_info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  nodec_vlogf(LOG_INFO, fmt, args);
  va_end(args);
}

void nodec_log_verbose(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  nodec_vlogf(LOG_VERBOSE, fmt, args);
  va_end(args);
}

void nodec_log_debug(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  nodec_vlogf(LOG_DEBUG, fmt, args);
  va_end(args);
}

void nodec_log_silly(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  nodec_vlogf(LOG_SILLY, fmt, args);
  va_end(args);
}
