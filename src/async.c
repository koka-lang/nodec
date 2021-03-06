#include "nodec.h"
#include "nodec-primitive.h"
#include "nodec-internal.h"
#include <assert.h> 
#include <string.h>

/* todos:
- default handling operations (so cancel_async becomes more efficient)
- single timer for setImmediate and yielding (to make them more lightweight)
*/

// forwards

typedef struct _cancel_scope_t cancel_scope_t;
typedef struct _async_request_t async_request_t;
typedef struct _async_local_t async_local_t;

async_request_t* async_request_alloc(uv_req_t* uvreq, bool nocancel, uint64_t timeout, void* owner);

/*-----------------------------------------------------------------
Async effect operations
-----------------------------------------------------------------*/
typedef uv_loop_t*            uv_loop_ptr;
typedef uv_req_t*             uv_req_ptr;
typedef uv_handle_t*          uv_handle_ptr;
typedef async_request_t*      async_request_ptr;
typedef const cancel_scope_t* cancel_scope_ptr;

#define lh_uv_loop_ptr_value(v)         ((uv_loop_t*)lh_ptr_value(v))
#define lh_value_uv_loop_ptr(h)         lh_value_ptr(h)
#define lh_async_request_ptr_value(v)   ((async_request_t*)lh_ptr_value(v))
#define lh_value_async_request_ptr(r)   lh_value_ptr(r)
#define lh_cancel_scope_ptr_value(v)    ((const cancel_scope_t*)lh_ptr_value(v))
#define lh_value_cancel_scope_ptr(r)    lh_value_ptr(r)

LH_DEFINE_EFFECT5(async, req_await, uv_loop, req_register, uv_cancel, owner_release)
LH_DEFINE_OP0(async, uv_loop, uv_loop_ptr)
LH_DEFINE_OP1(async, req_await, int, async_request_ptr)
LH_DEFINE_VOIDOP1(async, req_register, async_request_ptr)
LH_DEFINE_VOIDOP1(async, uv_cancel, cancel_scope_ptr)
LH_DEFINE_VOIDOP1(async, owner_release, lh_voidptr)


// Wrappers around the primitive operations for async.
uv_loop_t* async_loop() {
  return async_uv_loop();
}

void nodec_owner_release(void* owner) {
  async_owner_release(owner);
}


uv_errno_t asyncx_nocancel_await(uv_req_t* uvreq) {
  async_request_t* req = async_request_alloc(uvreq,true,0,NULL);
  uv_errno_t err = async_req_await(req);
  assert(err != UV_ETHROWCANCEL);
  return err;
}

uv_errno_t asyncxx_await(uv_req_t* uvreq, uint64_t timeout, void* owner) {
  async_request_t* req = async_request_alloc(uvreq, false, timeout, owner);
  return async_req_await(req);
}


uv_errno_t asyncx_await(uv_req_t* uvreq, uint64_t timeout, void* owner) {
  uv_errno_t err = asyncxx_await(uvreq, timeout, owner); 
  if (err == UV_ETHROWCANCEL) {
    lh_throw_cancel();
  }
  return err;
}

uv_errno_t asyncx_await_once(uv_req_t* uvreq) {
  return asyncx_await(uvreq, 0, NULL);
}

void async_await_once(uv_req_t* uvreq) {
  nodec_check(asyncx_await_once(uvreq));
}

uv_errno_t asyncx_await_owned(uv_req_t* uvreq, void* owner) {
  return asyncx_await(uvreq, 0, owner);
}

void async_await_owned(uv_req_t* uvreq, void* owner) {
  nodec_check(asyncx_await_owned(uvreq,owner));
}




/*-----------------------------------------------------------------
Throw on errors
-----------------------------------------------------------------*/
nodec_errno_t nodec_error_from(long err, error_kind_t err_kind) {
  return ((nodec_errno_t)err_kind + err);
}

long nodec_error_to(nodec_errno_t err, error_kind_t err_kind) {
  return (err - (nodec_errno_t)err_kind);
}

bool nodec_is_error_of(nodec_errno_t err, error_kind_t err_kind) {
  return (err >= (err_kind - 0xFFFF) && err <= (err_kind + 0xFFFF));
}

#ifdef USE_MBEDTLS
#include <mbedtls/include/mbedtls/error.h>
#endif

void nodec_strerror(nodec_errno_t err, const char* msg, uv_buf_t buf) {
  const char* msgsep = (msg == NULL ? "" : ": ");
  if (msg == NULL) msg = "";
  const char* errmsg = "";
  char errmsg_buf[256];
  if (err >= 0) {
    errmsg = "unknown error";
  }
#ifdef USE_MBEDTLS
  else if (nodec_is_error_of(err, ERRKIND_MBEDTLS)) {
    int tls_err = nodec_error_to(err, ERRKIND_MBEDTLS);
    mbedtls_strerror(tls_err, errmsg_buf, 256); errmsg_buf[255] = 0;
    errmsg = errmsg_buf;
  }
#endif
  else if (nodec_is_error_of(err, ERRKIND_HTTP)) {
    http_status_t status = nodec_error_to(err, ERRKIND_HTTP);
    errmsg = nodec_http_status_str(status);
  }
  else {
    errmsg = uv_strerror(err);
  }
  snprintf(buf.base, buf.len, "%s%s%s", msg, msgsep, errmsg);
}

void nodec_throw_msg_data(nodec_errno_t err, const char* msg, void* data) {
  lh_exception* exn;
  char errmsg[256];
  if (err == UV_ETHROWCANCEL) {
    exn = lh_exception_alloc_cancel();
  }
  else {
    nodec_strerror(err, msg, nodec_buf(errmsg, 255));
    exn = lh_exception_alloc_strdup(err, errmsg);
  }
  exn->data = data;
  lh_throw(exn);
}

void nodec_throw(nodec_errno_t err) {
  nodec_throw_msg_data(err, NULL, NULL);
}

void nodec_throw_msg(nodec_errno_t err, const char* msg) {
  nodec_throw_msg_data(err, msg, NULL);
}


// Check an error result, throwing on error
void nodec_check(nodec_errno_t err) {
  if (err != 0) {
    nodec_throw(err);
  }
}

// Check an error result, throwing on error
void nodec_check_msg(nodec_errno_t err, const char* msg) {
  if (err != 0) {
    nodec_throw_msg(err, msg);
  }
}

void nodec_check_data(nodec_errno_t err, void* data) {
  if (err != 0) {
    nodec_throw_msg_data(err, NULL, data);
  }
}
void nodec_check_msg_data(nodec_errno_t err, const char* msg, void* data) {
  if (err != 0) {
    nodec_throw_msg_data(err, msg, data);
  }
}



/*-----------------------------------------------------------------
  Scopes
-----------------------------------------------------------------*/

struct _cancel_scope_t {
  const struct _cancel_scope_t* parent;
  bool canceled;
};

implicit_define(_cancel_scope)

static const cancel_scope_t* cancel_scope() {
  return (const cancel_scope_t*)lh_ptr_value(implicit_get(_cancel_scope));
}

lh_value _cancel_scope_alloc() {
  cancel_scope_t* scope = nodec_alloc(cancel_scope_t);
  scope->parent = cancel_scope();
  scope->canceled = false;
  return lh_value_ptr(scope);
}


// #define using_cancel_scope()        using_implicit_defer(nodec_freev,_cancel_scope_alloc(),_cancel_scope)
#define using_outer_cancel_scope()  using_implicit(lh_value_null,_cancel_scope)

static bool in_scope_of(const cancel_scope_t* scope, const cancel_scope_t* top ) {
  while (scope != NULL && scope != top) {
    scope = scope->parent;
  }
  return (scope == top);
}

void async_scoped_cancel_under(cancel_scope_t* scope) {
  scope->canceled = true;
  async_uv_cancel(scope);
}

void async_scoped_cancel() {
  async_scoped_cancel_under((cancel_scope_t*)cancel_scope());
}

bool async_scoped_is_canceled() {
  for (const cancel_scope_t* scope = cancel_scope(); scope != NULL; scope = scope->parent) {
    if (scope->canceled) return true;
  }
  return false;
}


/*-----------------------------------------------------------------
   Free requests
-----------------------------------------------------------------*/
void nodec_req_force_free(uv_req_t* uvreq) {
  if (uvreq != NULL) {
    if (uvreq->type == UV_FS) {
      uv_fs_req_cleanup((uv_fs_t*)uvreq);
    }
    nodec_free(uvreq);
  }
}

void nodec_req_force_freev(lh_value uvreq) {
  nodec_req_force_free(lh_ptr_value(uvreq));
}


// When deallocating a request, some special values in the data field will not deallocate 
// quite yet.

#define UVREQ_FREE_ON_RESUME          ((void*)(-3))
#define UVREQ_FREE_ON_OWNER_RELEASE   ((void*)(-1))

void nodec_req_free(uv_req_t* uvreq) {
  if ((uvreq != NULL && 
       uvreq->data != UVREQ_FREE_ON_OWNER_RELEASE) && 
       uvreq->data != UVREQ_FREE_ON_RESUME) 
  {
    nodec_req_force_free(uvreq);
  }
}

void nodec_req_freev(lh_value uvreq) {
  nodec_req_free(lh_ptr_value(uvreq));
}


/*-----------------------------------------------------------------
  Asynchronous requests
-----------------------------------------------------------------*/

typedef void(async_resume_fun)(lh_resume r, lh_value local, uv_req_t* req, int err);


// Every libuv request will have a pointer to our request structure in its
// `data` field. This allows us to resume to the `async_await` point.
// The actual resuming is done in the `resumefun` such that we can sometimes change
// the resume behaviour: normally it resumes but we can also emit results into 
// a channel for example.
struct _async_request_t {
  async_request_t*      next;
  async_request_t*      prev;
  lh_resume             resume;
  lh_value              local;
  const cancel_scope_t* scope;
  uv_req_t*             uvreq;
  uverr_t               canceled_err;
  void*                 owner;
  uint64_t              due;
  async_resume_fun*     resumefun;
};


async_request_t* async_request_alloc(uv_req_t* uvreq, bool nocancel, uint64_t timeout, void* owner) {
  async_request_t* req = nodec_zero_alloc(async_request_t);
  uvreq->data = req;
  req->uvreq = uvreq;
  req->owner = owner;
  req->scope = cancel_scope();
  if (timeout>0) {
    uint64_t now = async_loop()->time;
    req->due = (UINT64_MAX - now < timeout ? UINT64_MAX : now + timeout);
  }
  if (!nocancel || timeout!=0) async_req_register(req);
  return req;
}

static void async_request_free(async_request_t* req) {
  // unregister ourselves from outstanding requests
  async_request_t* prev = req->prev;
  if (req->prev != NULL) {    // only NULL for `nocancel` requests; otherwise always valid since we use a dummy head element
    prev->next = req->next;
    if (req->next != NULL) req->next->prev = prev; // link back
  }
  // and free
  nodec_free(req);
}

static void async_resume_default(lh_resume resume, lh_value local, uv_req_t* req, int err) {
  //lh_assert(r != NULL);
  if (resume != NULL) {
    lh_release_resume(resume, local, lh_value_int(err));
  }
}


static void async_request_resume(async_request_t* req, uv_req_t* uvreq, int err) {
  assert(uvreq!=NULL);
  assert(uvreq->data == req);
  assert(req->uvreq == NULL || req->uvreq == uvreq);
  if (req->uvreq != NULL && req->uvreq->data == req) { // invoke only once!
    // put resume arguments in locals
    async_resume_fun* resumefun = req->resumefun;
    lh_resume resume = req->resume;
    lh_value local = req->local;
    
    // if we were canceled explicitly, we cannot deallocate the orginal
    // request right away because it might still modified, or its callback
    // might be called once the request is satisfied. Therefore, if there
    // is an owner set, we put `UVREQ_FREE_ON_OWNER_RELEASE` in the 
    // uvrequest `data` field to signify
    // it should not yet be deallocated -- but only once the owner is
    // deallocated (usually a `uv_handle_t*`)._
    // We use `UVREQ_FREE_ON_RESUME` to signify to deallocate once the 
    // original callback is called.
    // this is for request that have no particular owner and where the callback
    // will be called! like most file system requests
    if (req->canceled_err!=0) {
      err = req->canceled_err;
      if (req->owner != NULL) {
        uvreq->data = UVREQ_FREE_ON_OWNER_RELEASE; // signify to not deallocate in `nodec_req_free`
        // we leave it in the outstanding request where it will be freed when
        // the `owner` (usually a `uv_handle_t`) gets released.
      }
      else {
        uvreq->data = UVREQ_FREE_ON_RESUME; // signify to not deallocate in `nodec_req_free`
        // we free the request object, and free the uv request once its callback
        // is called
        async_request_free(req);
      }
    }
    else {
      // unlink the request
      uvreq->data = NULL;
      async_request_free(req);
    }
    (*resumefun)(resume, local, uvreq, err);
  }
}



// The main entry point for regular request callbacks which will resume
// to the point where `async_await` was called on that request.
void async_req_resume(uv_req_t* uvreq, int err) {
  assert(uvreq != NULL);
  async_request_t* req = (async_request_t*)uvreq->data;
  if (req != NULL) {
    if (req == UVREQ_FREE_ON_RESUME) {
      // was explicitly canceled, deallocate now
      nodec_req_force_free(uvreq);
    }
    else if (req == UVREQ_FREE_ON_OWNER_RELEASE) {
      // was expliclitly canceled, but has an owner (usually a uv_handle_t) and
      // will be deallocated later when the owner is released
    }
    else {
      // regular resumption
      async_request_resume(req, uvreq, err);
    }
  }
}



/*-----------------------------------------------------------------
  Main Async Handler
-----------------------------------------------------------------*/

struct _async_local_t {
  uv_loop_t*      loop;      // current event loop
  async_request_t requests;  // empty request to be the head of the queue of outstanding requests
                             // these can include canceled request that are deallocated when
                             // their owner is released (`uvreq.data` == UVREQ_FREE_ON_OWNER_RELEASE)                              
  uv_timer_t*     periodic;  // interval timer. currently only used for timing out requests
                             // on a slow interval but in the future could be used for light
                             // weight timers sharing a single handle.
};

/*-----------------------------------------------------------------
  Timeouts and cancelation
-----------------------------------------------------------------*/

static void _periodic_force_timeout_cb(void* data) {
  uv_req_t* uvreq = (uv_req_t*)data;
  async_req_resume(uvreq, UV_ETIMEDOUT);
}

static void _periodic_cb(uv_timer_t* timer) {
  async_local_t* local = (async_local_t*)(timer->data);
  uint64_t now = timer->loop->time;
  uv_timer_again(timer);
  // check if any outstanding requests timed out.
  for (async_request_t* req = local->requests.next; req != NULL; req = req->next) {
    if (req->uvreq != NULL && req->canceled_err==0 && req->due != 0 && req->due < now) {
      // try primitive cancelation first; guarantees the callback is called with UV_ECANCELED
      req->canceled_err = UV_ETIMEDOUT;
      uv_errno_t err = uv_cancel(req->uvreq);
      if (err != 0) {
        // cancel failed; cancel it explicitly (through the eventloop instead using a 0 timeout)
        // this is risky as async_req_resume can be invoked twice and the first return might 
        // trigger deallocation of the uv_req_t structure which would be too early. Therefore,
        // we set its `data` field to `UVREQ_FREE_ON_XXX` and check for that before deallocating a request.
        _uv_set_timeout(local->loop, &_periodic_force_timeout_cb, req->uvreq, 0);
        // todo: dont ignore errors here?
      }
    }
  }
}

static void _explicit_cancel_cb(void* data) {
  uv_req_t* uvreq = (uv_req_t*)data;
  async_req_resume(uvreq, UV_ETHROWCANCEL);
}

static lh_value _async_uv_cancel(lh_resume resume, lh_value localv, lh_value scopev) {
  async_local_t* local = (async_local_t*)lh_ptr_value(localv);
  const cancel_scope_t* scope = lh_cancel_scope_ptr_value(scopev);
  for (async_request_t* req = local->requests.next; req != NULL; req = req->next) {
    if (req->uvreq != NULL && req->canceled_err==0 && in_scope_of(req->scope, scope)) {
      // try primitive cancelation first; guarantees the callback is called with UV_ECANCELED
      req->canceled_err = UV_ETHROWCANCEL;
      uv_errno_t err = uv_cancel(req->uvreq);
      if (err != 0) {
        // cancel failed; cancel it explicitly (through the eventloop instead using a 0 timeout)
        // this is risky as async_req_resume can be invoked twice and the first return might 
        // trigger deallocation of the uv_req_t structure which would be too early. Therefore,
        // we set its `data` field to `UVREQ_FREE_ON_XXX` and check for that before deallocating a request.
        _uv_set_timeout(local->loop, &_explicit_cancel_cb, req->uvreq, 0);
        // todo: dont ignore errors here?
      }
    }
  }
  return lh_tail_resume(resume, localv, lh_value_null);
}

/*-----------------------------------------------------------------
  Main Async Handler primitives
-----------------------------------------------------------------*/

// Await an asynchronous request
static lh_value _async_req_await(lh_resume resume, lh_value local, lh_value arg) {
  async_request_t* req = lh_async_request_ptr_value(arg);
  assert(req != NULL);
  assert(req->uvreq != NULL);
  assert(req->uvreq->data == req);
  req->local = local;
  req->resume = resume;
  if (req->resumefun==NULL) req->resumefun = &async_resume_default;
  return lh_value_null;  // this exits our async handler back to the main event loop
}

// Return the current libUV event loop
static lh_value _async_uv_loop(lh_resume r, lh_value localv, lh_value arg) {
  async_local_t* local = (async_local_t*)lh_ptr_value(localv);
  return lh_tail_resume(r, localv, lh_value_ptr(local->loop));
}

// Register an outstanding request
static lh_value _async_req_register(lh_resume r, lh_value localv, lh_value arg) {
  async_local_t* local = (async_local_t*)lh_ptr_value(localv);
  async_request_t* req = lh_async_request_ptr_value(arg);
  assert(req != NULL);
  // initialize periodic timer if necessary
  if (local->periodic==NULL && req->due != 0) {
    local->periodic = nodecx_zero_alloc(uv_timer_t);
    if (local->periodic != NULL) {
      uv_timer_init(local->loop, local->periodic);
      local->periodic->data = local;
      uv_timer_start(local->periodic, &_periodic_cb, 500, 500);
    }
  }
  // insert in front
  req->next = local->requests.next;
  if (req->next != NULL) req->next->prev = req;             // link back
  req->prev = &local->requests;   
  local->requests.next = req;
  return lh_tail_resume(r, localv, lh_value_null);
}

static bool uvreq_is_pending(uv_req_t* uvreq) {
  if (uvreq == NULL) return false;
  switch (uvreq->type) {
  case UV_WRITE: {
#ifdef _WIN32
    uv_stream_t* s = ((uv_write_t*)uvreq)->handle;
    bool pending = s->reqs_pending > 0;
#else
    // TODO: do the right check for unix
    bool pending = true; // conservative for now
#endif
    return pending;
  }
  case UV_UNKNOWN_REQ: // Our internal requests for timers for example
    return false;  
  default: 
    return true; // conservate for now; this might delay deallocation of requests but is safe
  }
}

// Deallocate outstanding requests when their owner is released
// This usualy only happens for explicitly canceled requests
static lh_value _async_owner_release(lh_resume r, lh_value localv, lh_value arg) {
  async_local_t* local = (async_local_t*)lh_ptr_value(localv);
  void*          owner = lh_ptr_value(arg);
  if (owner != NULL) {
    for (async_request_t* req = local->requests.next; req != NULL; ) {  // TODO: linear lookup -- but probably ok.
      async_request_t* next = req->next;
      if (req->canceled_err != 0 && req->owner == owner) {
        assert(req->uvreq != NULL && req->uvreq->data == UVREQ_FREE_ON_OWNER_RELEASE);
        // We cannot always free a request at this point either.. it might
        // still be in the pending request queue of the uv event loop
        // we check for that first:
        if (uvreq_is_pending(req->uvreq)) {
          // assume it will be called later on, and set the data to `UVREQ_FREE_ON_RESUME` to be deallocated 
          // at that time
          req->uvreq->data = UVREQ_FREE_ON_RESUME;
        }
        else {
          // free it now
          nodec_req_force_free(req->uvreq);
        }
        async_request_free(req);  // will unlink properly
      }
      req = next;
    }
  }
  return lh_tail_resume(r, localv, lh_value_null);
}

static void _async_release(lh_value localv) {
  async_local_t* local = (async_local_t*)lh_ptr_value(localv);
  assert(local != NULL);
  assert(local->requests.next == NULL);
  // stop reaper interval
  if (local->periodic!=NULL) {
    uv_timer_stop(local->periodic);
    nodec_free(local->periodic);
    local->periodic = NULL;
  }
  // paranoia: clean up any left over outstanding requests
  for (async_request_t* req = local->requests.next; req != NULL; ) { 
    async_request_t* next = req->next;
    nodec_req_force_free(req->uvreq);
    async_request_free(req);  // will unlink properly
    req = next;
  }
  nodec_free(local);
}



// The main async handler
static const lh_operation _async_ops[] = {
  { LH_OP_GENERAL, LH_OPTAG(async,req_await), &_async_req_await },
  { LH_OP_TAIL_NOOP, LH_OPTAG(async,uv_loop), &_async_uv_loop },
  { LH_OP_TAIL_NOOP, LH_OPTAG(async,req_register), &_async_req_register },
  { LH_OP_TAIL_NOOP, LH_OPTAG(async,uv_cancel), &_async_uv_cancel },
  { LH_OP_TAIL_NOOP, LH_OPTAG(async,owner_release), &_async_owner_release },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef _async_def = { LH_EFFECT(async), NULL, _async_release, NULL, _async_ops };


lh_value async_handler(uv_loop_t* loop, lh_value(*action)(lh_value), lh_value arg) {
  async_local_t* local = nodecx_zero_alloc(async_local_t);
  if (local == NULL) return lh_value_null;
  local->loop = loop;
  local->requests.next = NULL;
  local->requests.prev = NULL;
  return lh_handle(&_async_def, lh_value_ptr(local), action, arg);  
}


/*-----------------------------------------------------------------
  Channel async handler for interleave
-----------------------------------------------------------------*/

static lh_value _channel_async_req_await(lh_resume r, lh_value local, lh_value arg) {
  async_request_t* req = lh_async_request_ptr_value(arg);
  assert(req != NULL);
  assert(req->uvreq != NULL);
  assert(req->uvreq->data == req);
  req->resume = r;
  req->local = local;
  if (req->resumefun==NULL) req->resumefun = &_channel_async_req_resume;
  return lh_value_null;  // exit to our local async handler back to interleaved
}

static const lh_operation _channel_async_ops[] = {
  { LH_OP_GENERAL, LH_OPTAG(async,req_await), &_channel_async_req_await },
  { LH_OP_FORWARD, LH_OPTAG(async,uv_loop), NULL },
  { LH_OP_FORWARD, LH_OPTAG(async,req_register), NULL },
  { LH_OP_FORWARD, LH_OPTAG(async,uv_cancel), NULL },
  { LH_OP_FORWARD, LH_OPTAG(async,owner_release), NULL },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef _channel_async_hdef = { LH_EFFECT(async), NULL, NULL, NULL, _channel_async_ops };

// The channel async handler
// Resume by emmitting a local resume into a channel
void _channel_async_req_resume(lh_resume r, lh_value local, uv_req_t* req, int err) {
  assert(r != NULL);
  assert(local != lh_value_null);
  if (r != NULL) {
    channel_emit((channel_t*)lh_ptr_value(local), lh_value_ptr(r), local, err);
  }
}

lh_value _channel_async_handler(channel_t* channel, lh_actionfun* action, lh_value arg) {
  return lh_handle(&_channel_async_hdef, lh_value_ptr(channel), action, arg);
}

/*-----------------------------------------------------------------
Main wrapper
-----------------------------------------------------------------*/
static lh_value uv_main_action(lh_value ventry) {
  nodec_main_fun_t* entry = (nodec_main_fun_t*)lh_fun_ptr_value(ventry);
  {using_tty() {
    {using_log(LOG_DEFAULT) {
      entry();
    }}
  }}
  return lh_value_null;
}

static lh_value uv_main_try_action(lh_value entry) {
  lh_exception* exn;
  {using_outer_cancel_scope() {
    lh_try(&exn, uv_main_action, entry);
    if (exn != NULL) {
      fprintf(stderr,"\nNodeC: unhandled exception: %s\n", exn->msg);
      lh_exception_free(exn);
    }
  }}
  return lh_value_null;
}

static void uv_main_cb(uv_timer_t* t_start) {
  async_handler(t_start->loop, &uv_main_try_action, lh_value_fun_ptr(*((nodec_main_fun_t**)t_start->data)));
  nodec_timer_free(t_start,false);
}

uv_errno_t async_main( nodec_main_fun_t* entry  ) {
  uv_replace_allocator(&_nodecx_malloc, &_nodecx_realloc, &_nodecx_calloc, (void(*)(void*))(&_nodec_free));
  uv_loop_t* loop = nodecx_zero_alloc(uv_loop_t);
  if (loop == NULL) return UV_ENOMEM;
  uv_errno_t err = uv_loop_init(loop);
  if (err == 0) {
    uv_timer_t* t_start = nodecx_zero_alloc(uv_timer_t);
    if (t_start == NULL) err = UV_ENOMEM;
    if (err == 0) {
      err = uv_timer_init(loop, t_start);
      if (err == 0) {
        t_start->data = &entry;  
        err = uv_timer_start(t_start, &uv_main_cb, 0, 0);
        if (err == 0) {
          // printf("starting event loop\n");
          err = uv_run(loop, UV_RUN_DEFAULT);
          t_start = NULL;
        }
      }
    }
    if (t_start != NULL) nodec_free(t_start);
  }
  uv_loop_close(loop);
  nodec_free(loop);
  nodec_check_memory();
  lh_debug_wait_for_enter();
  return err;
}
