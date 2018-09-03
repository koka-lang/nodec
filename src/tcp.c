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

void nodec_sockname(const struct sockaddr* addr, char* buf, size_t bufsize) {
  buf[0] = 0;
  if (addr != NULL) {
    if (addr->sa_family == AF_INET6) {
      uv_ip6_name((const struct sockaddr_in6*)addr, buf, bufsize);
    }
    else {
      uv_ip4_name((const struct sockaddr_in*)addr, buf, bufsize);
    }
  }
  buf[bufsize - 1] = 0;
}

void check_uv_err_addr(int err, const struct sockaddr* addr) {
  // todo: switch to ipv6 address if needed
  if (err != 0) {
    char buf[256];
    nodec_sockname(addr, buf, sizeof(buf));
    nodec_check_msg(err, buf);
  }
}

/*-----------------------------------------------------------------
  handling tcp
-----------------------------------------------------------------*/


void nodec_tcp_free(uv_tcp_t* tcp) {
  nodec_uv_stream_free((uv_stream_t*)tcp);
}

void nodec_tcp_freev(lh_value tcp) {
  nodec_tcp_free(lh_ptr_value(tcp));
}

uv_tcp_t* nodec_tcp_alloc() {
  uv_tcp_t* tcp = nodec_zero_alloc(uv_tcp_t);
  nodec_check( uv_tcp_init(async_loop(), tcp) );  
  return tcp;
}


void nodec_tcp_bind(uv_tcp_t* handle, const struct sockaddr* addr, unsigned int flags) {
  check_uv_err_addr(uv_tcp_bind(handle, addr, flags), addr);
} 

static void _listen_cb(uv_stream_t* server, int status) {
#ifndef NDEBUG
  fprintf(stderr, "connection request\n");
#endif
  uv_tcp_t* client = NULL;
  channel_t* ch = NULL;
  int err = 0;
  if (status != 0) {
    err = status;
  }
  else if (server==NULL) {
    err = UV_EINVAL;
  }
  else if ((ch = (channel_t*)server->data) == NULL) {
    err = UV_EINVAL;
  }
  else if (channel_is_full(ch)) {
    err = UV_ENOSPC; // stop accepting connections if the queue is full
  }
  else {
    client = (uv_tcp_t*)nodecx_calloc(1,sizeof(uv_tcp_t));
    if (client == NULL) {
      err = UV_ENOMEM;
    }
    else {
      err = uv_tcp_init(server->loop, client);
      if (err == 0) {
        err = uv_accept(server, (uv_stream_t*)client);
        if (err == 0) {
          // here we emit into the channel
          // this will either queue the element, or call a listener
          // entering a listener is ok since that will be a resume 
          // under an async/try handler again.
          // TODO: we should have a size limited queue and check the emit return value
      #ifndef NDEBUG
          fprintf(stderr, "connection accepted, emitted in channel\n");
      #endif
          err = channel_emit(ch, lh_value_ptr(client), lh_value_null, 0);  // if err==UV_NOSPC the channel was full
        }
      }
    }
  }
  if (err!=0) {
    // deallocate client on error
    if (client!=NULL) {
      nodec_uv_stream_free((uv_stream_t*)client);
      client = NULL;
    }
    fprintf(stderr, "connection error: %i: %s\n", err, uv_strerror(err));
  }
}

// Free TCP stream associated with a tcp channel
static void _channel_release_tcp(lh_value tcpv) {
  uv_tcp_t* tcp = (uv_tcp_t*)lh_ptr_value(tcpv);
  tcp->data = NULL; // is the channel itself; don't free it in the stream_free
  nodec_tcp_free(tcp);
}

static void _channel_release_client(lh_value data, lh_value arg, int err) {
  uv_stream_t* client = (uv_stream_t*)lh_ptr_value(data);
  if (client != NULL) {
    nodec_uv_stream_free(client);    
  }
}

tcp_channel_t* nodec_tcp_listen(uv_tcp_t* tcp, int backlog, bool channel_owns_tcp) {
  if (backlog <= 0) backlog = 128;
  nodec_check(uv_listen((uv_stream_t*)tcp, backlog, &_listen_cb));
  tcp_channel_t* ch = (tcp_channel_t*)channel_alloc_ex(8, // TODO: should be small?
                          (channel_owns_tcp ? &_channel_release_tcp : NULL), 
                              lh_value_ptr(tcp), &_channel_release_client );  
  tcp->data = ch;
  return ch;
}

void nodec_ip4_addr(const char* ip, int port, struct sockaddr_in* addr) {
  nodec_check(uv_ip4_addr(ip, port, addr));
}

void nodec_ip6_addr(const char* ip, int port, struct sockaddr_in6* addr) {
  nodec_check(uv_ip6_addr(ip, port, addr));
}

tcp_channel_t* nodec_tcp_listen_at(const struct sockaddr* addr, int backlog) {
  uv_tcp_t* tcp = nodec_tcp_alloc();
  tcp_channel_t* ch = NULL;
  {on_abort(nodec_tcp_freev, lh_value_ptr(tcp)) {
    nodec_tcp_bind(tcp, addr, 0);
    ch = nodec_tcp_listen(tcp, backlog, true);
  }}
  return ch;
}

struct sockaddr* nodec_parse_sockaddr(const char* host) {
  struct sockaddr*    addr = NULL;
  nodec_url_t* url = nodec_parse_host(host);
  {using_url(url) {
    if (nodec_url_is_ip6(url)) {
      struct sockaddr_in6* addr6 = nodec_zero_alloc(struct sockaddr_in6);
      nodec_ip6_addr(nodec_url_host(url), nodec_url_port(url), addr6);
      addr = (struct sockaddr*)addr6;
    }
    else {
      struct sockaddr_in* addr4 = nodec_zero_alloc(struct sockaddr_in);
      nodec_ip4_addr(nodec_url_host(url), nodec_url_port(url), addr4);
      addr = (struct sockaddr*)addr4;
    }
  }}
  return addr;
}


uv_stream_t* async_tcp_channel_receive(tcp_channel_t* ch) {
  lh_value data = lh_value_null;
  channel_receive(ch, &data, NULL);
  //printf("got a connection!\n");
  return (uv_stream_t*)lh_ptr_value(data);
}


static void connect_cb(uv_connect_t* req, int status) {
  async_req_resume((uv_req_t*)req, status >= 0 ? 0 : status);
}

nodec_bstream_t* async_tcp_connect_at(const struct sockaddr* addr, const char* host) {
  uv_tcp_t* tcp = nodec_tcp_alloc();
  {on_abort(nodec_tcp_freev, lh_value_ptr(tcp)) {
    {using_req(uv_connect_t, req) {
      nodec_check_msg(uv_tcp_connect(req, tcp, addr, &connect_cb), host);
      nodec_check_msg(asyncx_await_once((uv_req_t*)req),host);
    }}
  }}
  return nodec_bstream_alloc_read( (uv_stream_t*)tcp );
}

nodec_bstream_t* async_tcp_connect_at_host(const char* host, const char* service) {
  struct addrinfo* info = async_getaddrinfo(host, (service==NULL ? "http" : service), NULL);
  if (info==NULL) nodec_check_msg(UV_EAI_NONAME,host);
  nodec_bstream_t* tcp = NULL;
  {using_addrinfo(info) {
    tcp = async_tcp_connect_at(info->ai_addr, host);
  }}
  return tcp;
}

nodec_bstream_t* async_tcp_connect(const char* host) {
  nodec_url_t* url = nodec_parse_url(host);
  nodec_bstream_t* stream = NULL;
  {using_url(url) {
    const char* port = nodec_url_port_str(url);
    if (port == NULL) port = nodec_url_schema(url);
    stream = async_tcp_connect_at_host(nodec_url_host(url), port);
  }}
  return stream;
}

/*-----------------------------------------------------------------
    A TCP server
-----------------------------------------------------------------*/

static lh_value async_log_tcp_exn(lh_value exnv) {
  lh_exception* exn = (lh_exception*)lh_ptr_value(exnv);
  if (exn == NULL || exn->data == NULL) return lh_value_null;
  fprintf(stderr, "tcp server error: %i: %s\n", exn->code, (exn->msg == NULL ? "unknown" : exn->msg));
  return lh_value_null;
}




typedef struct _tcp_serve_args {
  nodec_tcp_connection_wrap_t* connection_wrap;
  nodec_tcp_connection_fun_t* serve;
  lh_value            serve_arg;
  lh_value            wrap_arg;
  tcp_channel_t*      ch;
  size_t              max_interleaving;
  uint64_t            timeout_total;
  uint64_t            timeout_keepalive;
  lh_actionfun*       on_exn;
  uv_stream_t*        uvclient;
} tcp_serve_args;


static lh_value tcp_connection(lh_value argsv) {
  tcp_connection_args* args = (tcp_connection_args*)lh_ptr_value(argsv);
  args->connection_fun(args->id, args->client, args->arg);
  return lh_value_null;
}

static lh_value tcp_connection_timeout(lh_value argsv) {
  tcp_connection_args* args = (tcp_connection_args*)lh_ptr_value(argsv);
  if (args->timeout_total == 0) {
    return tcp_connection(argsv);
  }
  else {
    bool timedout = false;
    lh_value result = async_timeout(&tcp_connection, argsv, args->timeout_total, &timedout);
    if (timedout) throw_http_err(408);
    return result;
  }
}

static lh_value tcp_connection_keepalive(lh_value argsv) {
  tcp_connection_args* args = (tcp_connection_args*)lh_ptr_value(argsv);
  if (args->timeout_keepalive == 0) {
    return tcp_connection_timeout(argsv);
  }
  else {
    lh_value result = lh_value_null;
    uverr_t err = 0;
    //nodec_check(uv_tcp_keepalive((uv_tcp_t*)args->client, 1, (unsigned)args->keepalive));
    fprintf(stderr, "use keep alive connection %i.\n", args->id);
    do {
      result = tcp_connection_timeout(argsv);
      err = asyncx_uv_stream_await_available(args->uvclient, args->timeout_keepalive);
    } while (err == 0);
    fprintf(stderr, "closed keep alive connection %i.\n", args->id);
    return result;
  }
}

void nodec_tcp_connection_wrap(const tcp_connection_args* args, lh_value ignored_arg) {
  lh_exception* exn;
  lh_try(&exn, &tcp_connection_keepalive, lh_value_any_ptr(args));
  if (exn != NULL) {
    // ignore closed client connections..
    if (!(exn->data == args->uvclient && exn->code == UV_ECANCELED)) {
      // send an exception response
      // wrap in try itself in case writing gives an error too!
      lh_exception* wrap = lh_exception_alloc(exn->code, exn->msg);
      wrap->data = args->client;
      lh_exception* ignore_exn = NULL;
      lh_try(&ignore_exn, args->on_exn, lh_value_any_ptr(wrap));
      lh_exception_free(wrap);
      lh_exception_free(ignore_exn);
    }
    lh_exception_free(exn);
  }
}


static lh_value tcp_serve_connection(lh_value argsv) {
  static int id = 0;
  tcp_serve_args args = *((tcp_serve_args*)lh_ptr_value(argsv)); // copy by value
  nodec_uv_stream_t* client = nodec_uv_stream_alloc(args.uvclient);    
  {using_uv_stream(client) {
    // TODO: what if an exception happens here?
    // TODO: make initial read allocation a parameter? Maybe needs to be enlarged for https?
    nodec_uv_stream_read_start(client, 8 * 1024, 0); // initial allocation at 8kb (for the header)
    tcp_connection_args cargs = { args.serve, id++, as_bstream(client), args.serve_arg, args.timeout_total, args.timeout_keepalive, args.on_exn, client };
    (*args.connection_wrap)(&cargs, args.wrap_arg);
  }}
  return lh_value_null;
}

static lh_value tcp_servev(lh_value argsv) {
  tcp_serve_args args = *((tcp_serve_args*)lh_ptr_value(argsv));
  do {
    uv_stream_t* uvclient = async_tcp_channel_receive(args.ch);
    if (nodec_current_strand_count() > args.max_interleaving) {
      // if too much concurrency, close the connection immediately
      nodec_uv_stream_free(uvclient);
    }
    else {
      tcp_serve_args sargs = args; // copy
      sargs.uvclient = uvclient;
      async_strand_create(&tcp_serve_connection, lh_value_any_ptr(&sargs), NULL);
    }
  } while (true);  // should be until termination
  return lh_value_null;
}

void async_tcp_server_at_ex(const struct sockaddr* addr, 
  tcp_server_config_t* config,
  nodec_tcp_connection_fun_t* servefun, 
  nodec_tcp_connection_wrap_t* wrapfun,
  lh_actionfun* on_exn,
  lh_value serve_arg,
  lh_value wrap_arg)
{
  tcp_server_config_t default_config = tcp_server_config();
  if (config == NULL) config = &default_config;
  tcp_channel_t* ch = nodec_tcp_listen_at(addr, config->backlog);
  {using_tcp_channel(ch) {
    {using_zero_alloc(tcp_serve_args, sargs) {
      sargs->connection_wrap = wrapfun;
      sargs->wrap_arg = wrap_arg;
      sargs->serve = servefun;
      sargs->serve_arg = serve_arg;
      sargs->ch = ch;
      sargs->max_interleaving = config->max_interleaving;
      sargs->timeout_total = config->timeout_total;
      sargs->timeout_keepalive = config->timeout;
      sargs->on_exn = (on_exn == NULL ? &async_log_tcp_exn : on_exn);
      async_interleave_dynamic(&tcp_servev, lh_value_ptr(sargs));
    }}
  }}
}

void async_tcp_server_at(const struct sockaddr* addr,
  tcp_server_config_t* config,
  nodec_tcp_connection_fun_t* servefun,
  lh_actionfun* on_exn,
  lh_value arg)
{
  async_tcp_server_at_ex(addr, config, servefun, 
    &nodec_tcp_connection_wrap, on_exn, arg, lh_value_null);
}