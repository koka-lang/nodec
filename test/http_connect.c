#include <stdio.h>
#include <nodec.h>
#include "test.h"

static const char* http_request =
  "GET / HTTP/1.1\r\n"
  "Host: www.bing.com\r\n"
  "Connection: close\r\n"
  "\r\n";

static lh_value test_connection(http_in_t* in, http_out_t* out, lh_value arg) {
  http_out_add_header(out, "Connection", "close");
  http_out_add_header(out, "Accept-Encoding", "gzip");
  http_out_send_request(out, HTTP_GET, "/");
  async_http_in_read_headers(in); // wait for response
  const size_t len = http_in_content_length(in);
  nodec_log_debug("received, status: %i, content length: %llu", http_in_status(in), (unsigned long long)len);
  //http_in_status_print(in);
  return lh_value_null;
}

static bool test_connect_worker() {
  lh_value rc = async_http_connect("www.bing.com", test_connection, lh_value_null);
  return true;
}

TEST_IMPL(connect) {
  CHECK(test_connect_worker());
  TEST_IMPL_END;
}
