#include <stdio.h>
#include <nodec.h>
#include "test.h"

static const char* const URL = "www.bing.com";

// This shows a disparity between the Content-Length and the base.len
// static const char* const URL = "www.google.com";

// This throws an exception in async_shutdown
// static const char* const URL = "www.amazon.com";

struct _DATA {
  bool rc;
};

static void print_headers(http_in_t* in) {
  size_t iter = 0;
  const char* value;
  const char* name;
  printf("\n\n\"Headers\": {\n");
  while ((name = http_in_header_next(in, &value, &iter))) {
    if (iter > 1) {
      printf(",\n");
    }
    printf("  \"%s\":\"%s\"", name, value);
  }
  printf("\n}\n\n");
}

static bool check_encoding_is_gzip(http_in_t* in) {
  const char* enc = http_in_header(in, "Content-Encoding");
  if (enc == NULL)
    return false;
  if (strcmp(enc, "gzip") != 0)
    return false;
  return true;
}

static bool check_the_header(http_in_t* in) {
  const size_t len = http_in_content_length(in);
  if (len == 0)
    return false;
  const http_status_t status = http_in_status(in);
  if (status != HTTP_STATUS_FOUND && 
      status != HTTP_STATUS_OK &&
      status != HTTP_STATUS_MOVED_PERMANENTLY
    )
    return false;
  print_headers(in);
  //if (!check_encoding_is_gzip(in))
  //  return false;
  return true;
}

static bool check_the_body(http_in_t* in) {
  uv_buf_t buf = async_http_in_read_body(in, 4 * NODEC_MB);
  if (buf.len == 0)
    return false;
  {using_buf(&buf) {
      printf("  buf.base: %p\n", buf.base);
      printf("  buf.len: %lu\n", buf.len);
  }}
  return true;
}

static bool check_http_response(http_in_t* in) {
  if (!check_the_header(in))
    return false;
  if (!check_the_body(in))
    return false;
  return true;
}

static void set_result(lh_value data, bool rc) {
  ((struct _DATA*) lh_ptr_value(data))->rc = rc;
}

static lh_value test_connection(http_in_t* in, http_out_t* out, lh_value data) {
  bool ans = true;
  http_out_add_header(out, "Connection", "close");
  http_out_add_header(out, "Accept-Encoding", "gzip");
  http_out_send_request(out, HTTP_GET, "/");
  size_t const asize = async_http_in_read_headers(in); // wait for response
  set_result(data, check_http_response(in));
  return lh_value_null;
}

static bool test_connect_worker() {
  bool ans = false;
  uv_buf_t buf = nodec_buf_alloc(sizeof(struct _DATA));
  {using_buf(&buf) {
    struct _DATA* data = (struct _DATA*)buf.base;
    data->rc = false;
    async_http_connect(URL, test_connection, lh_value_ptr(data));
    ans = data->rc;
  }}
  return ans;
}

TEST_IMPL(connect) {
  CHECK(test_connect_worker());
  TEST_IMPL_END;
}
