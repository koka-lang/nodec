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

typedef struct _https_server_args_t {
  nodec_http_servefun*  servefun;
  uint64_t              timeout_total;
  nodec_ssl_config_t*   ssl_config;
} https_server_args_t;


static void _nodec_https_serve(int id, nodec_bstream_t* encrypted_client, lh_value argsv) {
  https_server_args_t* args = (https_server_args_t*)lh_ptr_value(argsv);
  nodec_bstream_t* client = nodec_tls_stream_alloc(encrypted_client, args->ssl_config);
  {using_bstream(client) {
    // TODO: install exception handler and timeout functions
    _nodec_http_serve(id, client, lh_value_ptr(args->servefun));
  }}
}

void async_https_server_at(const char* host, tcp_server_config_t* tcp_config, nodec_ssl_config_t* ssl_config, nodec_http_servefun* servefun) {
  tcp_server_config_t default_config = tcp_server_config();
  if (tcp_config == NULL) tcp_config = &default_config;
  https_server_args_t args = { servefun, tcp_config->timeout_total, ssl_config };
  tcp_config->timeout_total = 0;
  struct sockaddr* addr = nodec_parse_sockaddr(host);
  {using_sockaddr(addr) {
    async_tcp_server_at(addr, tcp_config, &_nodec_https_serve,
      &async_write_http_exnv, lh_value_any_ptr(&args)); 
  }}
}