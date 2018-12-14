/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include <nodec.h>
#include "examples.h"

/*-----------------------------------------------------------------
  Example of a static http server
-----------------------------------------------------------------*/

static void http_serve() {
#ifndef NDEBUG
  fprintf(stderr,"\nrequest url: %s, content length: %llu\n", http_req_url(), (unsigned long long)http_req_content_length());
  http_req_print();
#endif
  http_serve_static( "./examples/web", NULL );
}

static void http_server() {
  const char* host = "127.0.0.1:8080";
  fprintf(stderr,"serving at: %s\n", host);
  async_http_server_at( host, NULL, &http_serve);
}

void ex_http_server_static() {
  async_stop_on_enter(&http_server);
}
