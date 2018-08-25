/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "cenv.h"
#include "nodec.h"
#include "nodec-primitive.h"
#include "nodec-internal.h"
#include <assert.h>
#include <time.h>

static uv_handle_t* handle_of_timer(uv_timer_t* timer) {
  return (uv_handle_t*)timer;
}

uv_timer_t* nodec_timer_alloc() {
  uv_timer_t* timer = nodec_zero_alloc(uv_timer_t);
  nodec_check(uv_timer_init(async_loop(), timer));
  return timer;
}

static void _timer_close_cb(uv_handle_t* timer) {
  nodec_free(timer);
}

void nodec_timer_close(uv_timer_t* timer) {
  if (timer!=NULL) {
    uv_close(handle_of_timer(timer), &_timer_close_cb);
  }
}

void nodec_timer_free(uv_timer_t* timer, bool owner_release) {
  nodec_timer_close(timer);
  if (owner_release) nodec_owner_release(timer);  
}

void nodec_timer_freev(lh_value timerv) {
  nodec_timer_free((uv_timer_t*)lh_ptr_value(timerv),true);
}

static void _async_timer_resume(uv_timer_t* timer) {
  uv_req_t* req = (uv_req_t*)timer->data;
  async_req_resume(req, 0);
}

void async_wait(uint64_t timeout) {
  uv_timer_t* timer = nodec_timer_alloc();
  {defer(nodec_timer_freev, lh_value_ptr(timer)) {
    {using_req(uv_req_t, req) {  // use a dummy request so we can await the timer handle
      timer->data = req;
      nodec_check(uv_timer_start(timer, &_async_timer_resume, timeout, 0));
      async_await_owned(req, timer);
    }}
  }}
}

void async_yield() {
  async_wait(0);
}


/* ----------------------------------------------------------------------------
  Internal timeout routine to delay certain function calls.
  Used for cancelation resumptions
-----------------------------------------------------------------------------*/

typedef struct _timeout_args {
  uv_timeoutfun* cb;
  void*          arg;
} timeout_args;

static void _timeout_cb(uv_timer_t* timer) {
  if (timer==NULL) return;
  timeout_args args = *((timeout_args*)timer->data);
  nodec_free(timer->data);
  nodec_timer_free(timer, false);
  args.cb(args.arg);
}

uv_errno_t _uv_set_timeout(uv_loop_t* loop, uv_timeoutfun* cb, void* arg, uint64_t timeout) {
  uv_timer_t* timer = nodecx_alloc(uv_timer_t);
  if (timer == NULL) return UV_ENOMEM;
  timeout_args* args = nodecx_alloc(timeout_args);
  if (args==NULL) { nodec_free(timer); return UV_ENOMEM; }
  nodec_zero(uv_timer_t, timer);
  uv_timer_init(loop, timer);
  args->cb = cb;
  args->arg = arg;
  timer->data = args;
  return uv_timer_start(timer, &_timeout_cb, timeout, 0);
}



/* ----------------------------------------------------------------------------
  Get current Date in fixed size RFC 1123 format; 
  updated at most once a second for efficiency
  Sun, 06 Nov 1994 08:49:37 GMT
-----------------------------------------------------------------------------*/

#ifdef HAS_GMTIME_S
int nodec_gmtime(struct tm* _tm, const time_t* const t) {
  return gmtime_s(_tm, t);
}
#else
int nodec_gmtime(struct tm* _tm, const time_t* const t) {
  *_tm = *(gmtime(t));
  return 0;
}
#endif

#ifdef HAS_LOCALTIME_S
int nodec_localtime(struct tm* _tm, const time_t* const t) {
  return localtime_s(_tm, t);
}
#else
int nodec_localtime(struct tm* _tm, const time_t* const t) {
  *_tm = *(localtime(t));
  return 0;
}
#endif


#ifdef HAS__MKGMTIME_
static time_t nodec_mkgmtime(struct tm* _tm) {
  return _mkgmtime(_tm);
}
#elif defined(HAS_TIMEGM) 
static time_t nodec_mkgmtime(struct tm* _tm) {
  return timegm(_tm);
}
#else
static time_t nodec_mkgmtime(struct tm * _tm) {
  // TODO: needs testing
  time_t t = mktime(_tm);

  struct tm gt;
  struct tm lt;
  nodec_gmtime(&gt,t);
  nodec_localtime(&lt,t); // normalize
  
  // adjust with the difference
  lt.tm_year -= gt.tm_year - lt.tm_year;
  lt.tm_mon  -= gt.tm_mon - lt.tm_mon;
  lt.tm_mday -= gt.tm_mday - lt.tm_mday;
  lt.tm_hour -= gt.tm_hour - lt.tm_hour;
  lt.tm_min  -= gt.tm_min - lt.tm_min;
  lt.tm_sec  -= gt.tm_sec - lt.tm_sec;

  // and convert again
  return mktime(&lt);
}
#endif

#define INET_DATE_LEN 29

static const char* days[7] =
  { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char* months[12] =
  { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

const char* nodec_inet_date( time_t now )
{
  static char inet_date[INET_DATE_LEN + 1] = "Thu, 01 Jan 1972 00:00:00 GMT";
  static time_t inet_time = 0;

  if (now == inet_time) return inet_date;
  struct tm tm;
  nodec_gmtime(&tm, &now);
  strftime(inet_date, INET_DATE_LEN + 1, "---, %d --- %Y %H:%M:%S GMT", &tm);
  if (tm.tm_wday >= 0 && tm.tm_wday < 7) memcpy(inet_date, days[tm.tm_wday], 3);
  if (tm.tm_mon >= 0 && tm.tm_mon < 12) memcpy(inet_date + 8, months[tm.tm_mon], 3);
  return inet_date;
}

const char* nodec_inet_date_now() {
  time_t now;
  time(&now);
  return nodec_inet_date(now);
}

bool nodec_parse_inet_date(const char* date, time_t* t) {
  struct tm g;
  memset(&g, 0, sizeof(g));  
  char month[4];
  *t = 0;
  if (date == NULL || strlen(date) != INET_DATE_LEN) return false;
#ifdef HAS_SSCANF_S
  int res = sscanf_s(date, "%*3s, %2d %3s %4d %2d:%2d:%2d GMT",
                      &g.tm_mday, month, 4, &g.tm_year,
                      &g.tm_hour, &g.tm_min, &g.tm_sec);
#else
  int res = sscanf(date, "%*3s, %2d %3s %4d %2d:%2d:%2d GMT",
    &g.tm_mday, month, &g.tm_year,
    &g.tm_hour, &g.tm_min, &g.tm_sec);
#endif
  if (res == EOF) return false;
  
  month[3] = 0;
  for (int i = 0; i < 12; i++) {
    if (strncmp(month, months[i], 3) == 0) {
      g.tm_mon = i;
      break;
    }
  }
  g.tm_year -= 1900;
  *t = nodec_mkgmtime(&g);  // or 'timegm' ? 
  return true;
}
