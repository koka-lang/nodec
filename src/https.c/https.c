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

static void nodec_https_serve(int id, nodec_bstream_t* client, lh_value servefunv) {
  nodec_tls_stream_t* ts = (nodec_tls_stream_t*)client;
  nodec_tls_stream_handshake(ts);
  nodec_http_serve(id, client, servefunv);
}

static void https_connection_wrap(const tcp_connection_args* args, lh_value sslv) {
  nodec_ssl_config_t* ssl_config = (nodec_ssl_config_t*)lh_ptr_value(sslv);
  nodec_bstream_t* client = nodec_tls_stream_alloc(args->client, ssl_config);
  {using_bstream(client) {
    tcp_connection_args targs = *args;
    targs.client = client; // overwrite encrypted client 
    nodec_tcp_connection_wrap(&targs, lh_value_null);
  }}
}

void async_https_server_at(const char* host, tcp_server_config_t* tcp_config, nodec_ssl_config_t* ssl_config, nodec_http_servefun* servefun) {
  tcp_server_config_t default_config = tcp_server_config();
  if (tcp_config == NULL) tcp_config = &default_config;
  tcp_config->timeout_total = 0;
  struct sockaddr* addr = nodec_parse_sockaddr(host);
  {using_sockaddr(addr) {
    async_tcp_server_at_ex(addr, tcp_config,
      &nodec_https_serve,
      &https_connection_wrap,
      &async_write_http_exnv,
      lh_value_ptr(servefun),
      lh_value_any_ptr(ssl_config));
  }}
}