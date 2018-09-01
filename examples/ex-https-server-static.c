/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include <nodec.h>
#include "examples.h"

/*-----------------------------------------------------------------
  Example of a static HTTPS server
-----------------------------------------------------------------*/

static void https_serve() {
#ifndef NDEBUG
  fprintf(stderr, "\nstrand %i: secure request url: %s, content length: %llu\n", http_strand_id(), http_req_url(), (unsigned long long)http_req_content_length());
  http_req_print();
#endif
  http_serve_static("./examples/web", NULL);
}

static void https_server() {
  const char* host = "127.0.0.1:8443";
  fprintf(stderr, "securely serving at: %s\n", host);
  nodec_ssl_config_t* ssl_config = nodec_ssl_config_server_from(
    "./examples/data/nodec.crt", "./examples/data/nodec.key", "NodeC");
  {using_ssl_config(ssl_config) {
    async_https_server_at(host, NULL, ssl_config, &https_serve);
  }}
}

void ex_https_server_static() {
  async_stop_on_enter(&https_server);
}
