#include <stdio.h>
#include <stdlib.h>
#include <nodec.h>
#include "test.h"

/*-----------------------------------------------------------------
  test static http server
-----------------------------------------------------------------*/

#define HOST "127.0.0.1:8080"
#define OUTPUT_PATH "./out/out.txt"

static void http_serve() {
#ifndef NDEBUG
  const char* const fmt = "\nrequest url: %s, content length: %llu\n";
  fprintf(stderr, fmt, (unsigned long long)http_req_content_length());
  http_req_print();
#endif
  http_serve_static("./examples/web", NULL);
}

static void http_server() {
  fprintf(stderr, "serving at: %s\n", HOST);
  async_http_server_at(HOST, NULL, &http_serve);
}

const char* get_cmd_path() {
#pragma warning( suppress : 4996 )
  return getenv("ComSpec");
}

static void run_curl() {
  const char* const cmd_path = get_cmd_path();
  async_tty_write("press enter to quit...");
  _spawnl(_P_NOWAIT, cmd_path, "/C", "curl -v -I ", HOST, ">", OUTPUT_PATH, NULL); 
  const char* s = async_tty_readline();
  nodec_free(s);
  async_tty_write("canceling...");
}

#undef OUTPUT_PATH
#undef HOST

TEST_IMPL(http_serve) {
  async_firstof(&http_server, &run_curl);
  TEST_IMPL_END;
}