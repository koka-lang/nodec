/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include <nodec.h>
#include "examples.h"


/*-----------------------------------------------------------------
  Example of an HTTP connection
-----------------------------------------------------------------*/

static lh_value with_connection(http_in_t* in, http_out_t* out, lh_value arg) {
  http_out_add_header(out, "Connection", "close");
  http_out_add_header(out, "Accept-Encoding", "gzip");
  http_out_send_request(out, HTTP_GET, "/");
  async_http_in_read_headers(in); // wait for response
  printf("received, status: %i, content length: %llu\n", http_in_status(in), (unsigned long long)http_in_content_length(in));
  http_in_status_print(in);
  return lh_value_null;
}

void ex_http_connect() {
  async_http_connect("www.google.com", with_connection, lh_value_null);
}
