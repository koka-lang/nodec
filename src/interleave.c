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

/*-----------------------------------------------------------------
  Interleave
-----------------------------------------------------------------*/


static void isolate(lh_actionfun* action, lh_value arg, lh_value* xres, lh_exception** xexn, volatile size_t* todo) {
  lh_exception* exn = NULL;
  lh_value      res = lh_value_null;
  if (async_scoped_is_canceled()) {
    // an earlier strand called `cancel` already, don't start this strand
    if (xexn != NULL) exn = lh_exception_alloc_cancel();
  }
  else {
    res = lh_try_all(&exn, action, arg);
  }
  if (todo != NULL) *todo = *todo - 1;
  if (xres != NULL) *xres = res;
  if (xexn != NULL) {
    *xexn = exn;
  }
  else {
    if (exn != NULL) lh_exception_free(exn);
  }
}


typedef struct _isolate_args_t {
  lh_actionfun*  action;
  lh_value       arg;
  lh_value*      res;
  lh_exception** exception;
  volatile size_t* todo;
} isolate_args_t;

static lh_value isolate_action(lh_value vargs) {
  // copy args by value
  isolate_args_t* args = ((isolate_args_t*)lh_ptr_value(vargs));
  isolate(args->action, args->arg, args->res, args->exception, args->todo);
  return lh_value_null;
}

static void _handle_interleave_strand(channel_t* channel, isolate_args_t* args) {
  _channel_async_handler(channel, &isolate_action, lh_value_any_ptr(args));
}


LH_DEFINE_EFFECT2(strand,create,count)

lh_value async_strand_create(lh_actionfun* action, lh_value arg, lh_exception** exn) {
  return lh_yieldN(LH_OPTAG(strand, create), 3, lh_value_fun_ptr(action), arg, lh_value_any_ptr(exn));
}

size_t nodec_current_strand_count() {
  return (size_t)(lh_long_value(lh_yield(LH_OPTAG(strand, count), lh_value_null)));
}

typedef struct _strand_local_t {
  channel_t*       channel;
  volatile size_t* todo;
} strand_local_t;


static lh_value _strand_create(lh_resume resume, lh_value localv, lh_value argsv) {
  yieldargs* yargs = lh_yieldargs_value(resume, argsv);
  assert(yargs->argcount == 3);
  // execute action in the context just outside of the strand handler (and under the channel handler)
  strand_local_t* local = (strand_local_t*)lh_ptr_value(localv);
  lh_actionfun* action = (lh_actionfun*)lh_fun_ptr_value(yargs->args[0]);
  lh_exception** exn = (lh_exception**)lh_ptr_value(yargs->args[2]);
  *local->todo = *local->todo + 1;
  lh_value res = lh_value_null;
  isolate_args_t iargs = { action, yargs->args[1], &res, exn, local->todo };
  _handle_interleave_strand(local->channel, &iargs);
  // and return
  return lh_tail_resume(resume, localv, res);
}

static lh_value _strand_count(lh_resume resume, lh_value localv, lh_value arg) {
  strand_local_t* local = (strand_local_t*)lh_ptr_value(localv);
  return lh_tail_resume(resume, localv, lh_value_long(*local->todo));
}

static const lh_operation strand_ops[] = {
  { LH_OP_TAIL, LH_OPTAG(strand,create), &_strand_create},
  { LH_OP_TAIL_NOOP, LH_OPTAG(strand,count), &_strand_count },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef strand_def = { LH_EFFECT(strand), NULL, NULL, NULL, strand_ops };

static lh_value handle_strands(channel_t* channel, volatile size_t* todo, lh_actionfun action, lh_value arg) {
  lh_value res;
  {using_zero_alloc(strand_local_t, local) {
    local->channel = channel;
    local->todo = todo;
    res = lh_handle(&strand_def, lh_value_ptr(local), action, arg);
  }}
  return res;
}

typedef struct _strands_action_args_t {
  isolate_args_t iargs;
  channel_t*     channel;  
} strands_action_args_t;

static lh_value strands_action(lh_value argsv) {
  // run the action inside interleave strand itself
  strands_action_args_t* args = (strands_action_args_t*)lh_ptr_value(argsv);
  volatile size_t* todo = args->iargs.todo;
  channel_t* channel = args->channel;
  _handle_interleave_strand(channel, &args->iargs);
  while (*todo > 0) {
    // a receive should never be canceled since it should wait until
    // it children are canceled (and then continue). 
    lh_value resumev;
    lh_value arg;
    int err = channel_receive_nocancel(channel, &resumev, &arg);
    if (resumev != lh_value_null) { // can happen on cancel
      lh_release_resume((lh_resume)lh_ptr_value(resumev), arg, lh_value_int(err));
    }
  }
  return lh_value_null;
}

lh_value async_interleave_dynamic(lh_actionfun action, lh_value arg) {
  lh_value res = lh_value_null;
  lh_exception* exn = NULL;
  volatile size_t* todo = nodec_alloc(size_t);
  {using_free((void*)(todo)){
    *todo = 1;
    {using_channel(channel) {
      strands_action_args_t sargs = { { action, arg, &res, &exn, todo }, channel };
      handle_strands(channel,todo,&strands_action, lh_value_any_ptr(&sargs));            
    }}
  }}
  if (exn != NULL) lh_throw(exn);
  return res;
}


typedef struct _interleave_n_args_t{
  size_t n;
  lh_actionfun** actions;
  lh_value* arg_results;
  lh_exception** exceptions;
} interleave_n_args_t;

static lh_value interleave_n_action(lh_value argsv) {
  interleave_n_args_t* args = (interleave_n_args_t*)lh_ptr_value(argsv);
  for (size_t i = 0; i < args->n; i++) {
    args->arg_results[i] = async_strand_create(args->actions[i], args->arg_results[i], &args->exceptions[i]);
  }
  return lh_value_null;
}

static void  _async_interleave_n(size_t n, lh_actionfun** actions, lh_value* arg_results, lh_exception** exceptions) {
  interleave_n_args_t args = { n, actions, arg_results, exceptions };
  async_interleave_dynamic(&interleave_n_action, lh_value_any_ptr(&args));
}

static void nodec_free_if_notnull(lh_value pv) {
  if (pv!=lh_value_null) nodec_freev(pv);
}

void asyncx_interleave(size_t n, lh_actionfun* actions[], lh_value arg_results[], lh_exception* exceptions[]) {
  if (n == 0 || actions == NULL) return;

  //lh_exception* exn = NULL;
  lh_value* local_args = NULL;
  lh_exception** local_exns = NULL;
  if (arg_results==NULL) {
    local_args = nodec_calloc(n, sizeof(lh_value));
    arg_results = local_args;
  }
  {defer(&nodec_free_if_notnull, lh_value_ptr(local_args)) {
    if (exceptions==NULL) {
      local_exns = nodec_calloc(n, sizeof(lh_exception*));
      exceptions = local_exns;
    }
    {defer(&nodec_free_if_notnull, lh_value_ptr(local_exns)) {
      _async_interleave_n(n, actions, arg_results, exceptions);
    }}
  }}
}

void async_interleave(size_t n, lh_actionfun* actions[], lh_value arg_results[]) {
  if (n == 0 || actions == NULL) return;
  if (n == 1) {
    lh_value res = (actions[0])(arg_results==NULL ? lh_value_null : arg_results[0]);
    if (arg_results!=NULL) arg_results[0] = res;
  }
  else {
    lh_exception* exn = NULL;
    {using_zero_alloc_n(n, lh_exception*, exceptions) {
      asyncx_interleave(n, actions, arg_results, exceptions);
      // rethrow the first exception and release the others
      for (size_t i = 0; i < n; i++) {
        if (exceptions[i] != NULL) {
          if (exn == NULL) {
            exn = exceptions[i];
          }
          else {
            lh_exception_free(exceptions[i]);
          }
        }
      }
    }}
    if (exn != NULL) lh_throw(exn);
  }
}

typedef struct _firstof_args_t {
  lh_actionfun* action;
  lh_value      arg;
} firstof_args_t;

static void firstof_cancel(lh_value _arg) {
  async_scoped_cancel();
}

static lh_value firstof_action(lh_value argsv) {
  firstof_args_t* args = (firstof_args_t*)lh_ptr_value(argsv);
  return lh_finally( args->action, args->arg, &firstof_cancel, lh_value_null);
}

lh_value async_firstof_ex(lh_actionfun* action1, lh_value arg1, lh_actionfun* action2, lh_value arg2, bool* first, bool ignore_exn ) {
  firstof_args_t args[2]       = { {action1, arg1}, {action2, arg2} };
  lh_actionfun* actions[2]     = { &firstof_action, &firstof_action };
  lh_value      arg_results[2] = { lh_value_any_ptr(&args[0]), lh_value_any_ptr(&args[1]) };
  lh_exception* exceptions[2]  = { NULL, NULL };
  {using_cancel_scope() {
    asyncx_interleave(2, actions, arg_results, exceptions);
  }}

  // pick the winner
  int pick = 0;
  // ignore cancel exceptions
  if (lh_exception_is_cancel(exceptions[0])) pick = 1;
  else if (lh_exception_is_cancel(exceptions[1])) pick = 0;
  // if not ignoring exceptions, pick the first exception raising one
  else if (!ignore_exn) {
    if (exceptions[0] != NULL) pick = 0;
    else if (exceptions[1] != NULL) pick = 1;
  }
  // or pick the first one with a result
  else if (exceptions[0] != NULL) pick = 0;
  else pick = 1;

  // Return the result
  if (first) *first = (pick == 0);
  if (pick==1) {
    if (exceptions[0]!=NULL) lh_exception_free(exceptions[0]);
    if (exceptions[1]!=NULL) lh_throw(exceptions[1]);
    return arg_results[1];
  }
  else {
    if (exceptions[1]!=NULL) lh_exception_free(exceptions[1]);
    if (exceptions[0]!=NULL) lh_throw(exceptions[0]);
    return arg_results[0];
  }
}

lh_value _firstof_action(lh_value actionv) {
  nodec_actionfun_t* action = (nodec_actionfun_t*)lh_fun_ptr_value(actionv);
  action();
  return lh_value_null;
}

bool async_firstof(nodec_actionfun_t* action1, nodec_actionfun_t* action2) {
  bool first = false;
  async_firstof_ex(&_firstof_action, lh_value_fun_ptr(action1), &_firstof_action, lh_value_fun_ptr(action2), &first, false);
  return first;
}


static lh_value _timeout_wait(lh_value timeoutv) {
  uint64_t timeout = lh_uint64_t_value(timeoutv);
  async_wait(timeout);
  return lh_value_null;
}

lh_value async_timeout(lh_actionfun* action, lh_value arg, uint64_t timeout, bool* timedout) {
  return async_firstof_ex(_timeout_wait, lh_value_uint64_t(timeout), action, arg, timedout, false);
}