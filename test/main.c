
#include <stdio.h>
#include <nodec.h>
#include "test.h"


/*-----------------------------------------------------------------
  Run tests
-----------------------------------------------------------------*/

#pragma warning( suppress : 4005 )
#define TEST_ENTRYX(name,showoutput)  { #name, &TEST_NAME(name), showoutput },

static const test_info_t tests[] = {
  TEST_LIST
  { NULL, false }
};

static int run_test(int no, const test_info_t* test) {
  return test->fun();
}

static void run_tests() {
  int total = 0;
  const test_info_t* test;
  for (test = &tests[0]; test->name != NULL; test++) {
    total++;
  }
  printf("running %i tests...\n\n", total);
  int ok = 0;
  int skipped = 0;
  for (int todo = total; todo > 0; todo--) {
    test = &tests[total - todo];
    printf("\n%3i: running %s...\n", todo, test->name);
    int res = run_test(todo, test);
    if (res == RES_OK) {
      ok++;
      printf("%3i: success.\n", todo);
    }
    else if (res == RES_SKIP) {
      skipped++;
      printf("%3i: skipped.\n", todo);
    }
    else {
      printf("%3i: FAILED!\n", todo);

    }
  }
  int failed = total - ok - skipped;
  printf("\n---------------------------------\ntotal   : %i\nskipped : %i\nfailed  : %i\n", total, skipped, failed);
}


/*-----------------------------------------------------------------
  The following tests are old and need to be moved 
  to separate files
-----------------------------------------------------------------*/

/*-----------------------------------------------------------------
  Test cancel
-----------------------------------------------------------------*/

static lh_value test_cancel1(lh_value arg) {
  printf("starting work...\n");
  //test_interleave();
  printf("and waiting a bit.. (1s)\n");
  async_wait(1000);
  printf("done work\n");
}

static void test_cancel_timeout(uint64_t timeout) {
  bool timedout = false;
  lh_value res = async_timeout(&test_cancel1, lh_value_null, timeout, &timedout);
  if (timedout) {
    printf("timed out\n");
  }
  else {
    printf("finished with: %i\n", lh_int_value(res));
  }
}

static void test_cancel() {
  test_cancel_timeout(1000);
  test_cancel_timeout(1500);
}


/*-----------------------------------------------------------------
  TCP
-----------------------------------------------------------------*/

const char* response_headers =
"HTTP/1.1 200 OK\r\n"
"Server : NodeC/0.1 (windows-x64)\r\n"
"Content-Type : text/html; charset=utf-8\r\n"
"Connection : Closed\r\n";

const char* response_body =
"<!DOCTYPE html>"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"</head>\n"
"<body>\n"
"  <h1>Hello NodeC World!</h1>\n"
"</body>\n"
"</html>\n";



static void xtest_http_serve() {
  int strand_id = http_strand_id();
  // input
#ifndef NDEBUG
  fprintf(stderr, "strand %i request, url: %s, content length: %llu\n", strand_id, http_req_url(), (unsigned long long)http_req_content_length());
  http_req_print();
#endif
  // work
  //printf("waiting %i secs...\n", 2); 
  //async_wait(1000);
  //check_uverr(UV_EADDRINUSE);

  http_static_config_t config = http_static_default_config();
  config.use_last_modified = true;
  config.use_etag = false;
  //config.gzip_min_size = SIZE_MAX;
  //config.read_buf_size = 1024;
  http_serve_static("../../../nodec-bench/web"
    , &config);

  // response
  /*
  const char* accept = http_req_header("Accept");
  if (accept != NULL && strstr(accept, "text/html")) {
    http_resp_send_body_str(HTTP_STATUS_OK, response_body, "text/html");
  }
  else {
    http_resp_send_ok();
  }
  */
  //printf("request handled\n\n\n");
}

static void test_tcp() {
  //tcp_server_config_t config = tcp_server_config();
  //config.max_interleaving = 500;
  const char* host = "127.0.0.1:8080";
  printf("serving at: %s\n", host);
  async_http_server_at(host, NULL, &test_http_serve);
}

static void test_https() {
  //tcp_server_config_t config = tcp_server_config();
  //config.max_interleaving = 500;
  const char* host = "127.0.0.1:443";
  printf("serving at: %s\n", host);
  nodec_ssl_config_t* ssl_config = nodec_ssl_config_server_from("../../../nodec-bench/nodec.crt",
    "../../../nodec-bench/nodec.key", "NodeC");
  {using_ssl_config(ssl_config) {
    async_https_server_at(host, NULL, ssl_config, &test_http_serve);
  }}
}

static void wait_tty() {
  async_tty_write("press enter to quit the server...");
  const char* s = async_tty_readline();
  nodec_free(s);
  async_tty_write("canceling server...");

}

static void test_tcp_tty() {
  async_firstof(&test_tcp, &wait_tty);
  // printf( first ? "http server exited\n" : "http server was terminated by the user\n");
}

/*-----------------------------------------------------------------
  TTY
-----------------------------------------------------------------*/

static void test_tty() {
  async_tty_write("\033[41;37m" "what is your name?" "\033[0m" " ");
  const char* s = async_tty_readline();
  {using_free(s) {
    async_tty_printf("I got: %s\n", s);
  }}
  async_tty_write("\033[41;37m" "and your age?" "\033[0m" " ");
  s = async_tty_readline();
  {using_free(s) {
    async_tty_printf("Now I got: %s\n", s);
  }}
}

/*-----------------------------------------------------------------
  Client Test
-----------------------------------------------------------------*/
const char* http_request_parts[] = {
  "GET / HTTP/1.1\r\n",
  "Host: 127.0.0.1\r\n",
  "Connection: close\r\n",
  "\r\n",
  NULL
};

void test_as_client() {
  nodec_bstream_t* conn = async_tcp_connect("127.0.0.1:8080");
  {using_bstream(conn) {
    const char* s;
    for (size_t i = 0; (s = http_request_parts[i]) != NULL; i++) {
      printf("write: %s\n", s);
      async_write(as_stream(conn), s);
      async_wait(250);
    }
    printf("await response...\n");
    char* body = async_read_all(conn, 32 * NODEC_MB);
    {using_free(body) {
      printf("received:\n%s", body);
    }}
  }}
}

/*-----------------------------------------------------------------
  Main
-----------------------------------------------------------------*/

static void entry() {
  //void test_http();
  printf("in the main loop\n");
  //test_files();
  //test_interleave();
  //test_cancel();
  //test_tcp();
  test_tty();
  //test_scandir();
  //test_dns();
  //test_http();
  //test_as_client();
  //test_connect();
  //test_tcp_tty();
  //test_url();
  //test_https();
}

int main() {
  async_main(run_tests);
  return 0;
}