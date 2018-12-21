#include <stdio.h>
#include <nodec.h>
#include "test.h"

static const char* const URLS[] = {
  "www.bing.com",
  "www.google.com",   // Content-Length != base.len
  // "www.amazon.com, // exception thrown in async_shutdown
};

static const char* const SECURE_URLS[] = {
  "https://www.google.com",
  "https://www.bing.com",
  // "https://www.amazon.com", // this causes a problem
};

struct PARAMS {
  bool gzip;
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
  const http_status_t status = http_in_status(in);
  if (status != HTTP_STATUS_FOUND && 
      status != HTTP_STATUS_OK &&
      status != HTTP_STATUS_MOVED_PERMANENTLY
    )
    return false;
  print_headers(in);
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

static lh_value test_connection(http_in_t* in, http_out_t* out, lh_value param) {
  bool ans = true;
  struct PARAMS* const data = (struct PARAMS*) lh_ptr_value(param);
  http_out_add_header(out, "Connection", "close");
  if (data->gzip) {
    http_out_add_header(out, "Accept-Encoding", "gzip");
  }
  http_out_send_request(out, HTTP_GET, "/");
  size_t const asize = async_http_in_read_headers(in); // wait for response
  data->rc = check_http_response(in);
  return lh_value_null;
}

typedef  bool(*connect_fun_t)(struct PARAMS*, const char* const[]);

static bool test_non_secure_connection(struct PARAMS* data, const char* const urls[], size_t count) {
  bool ans = false;
  for (size_t i = 0; i < count; i++) {
    const char* const url = URLS[i];
    async_http_connect(url, test_connection, lh_value_ptr(data));
    ans = data->rc;
    if (ans == false)
      break;
  }
  return ans;
}

static bool test_secure_connection(struct PARAMS* data, const char* const urls[], size_t count) {
  bool ans = false;
  for (size_t i = 0; i < count; i++) {
    const char* const url = SECURE_URLS[i];
    nodec_ssl_config_t* ssl_config = nodec_ssl_config_client();
    {using_ssl_config(ssl_config) {
      async_ssl_config_add_system_certs(ssl_config);
      async_https_connect(ssl_config, url, test_connection, lh_value_ptr(data));
    }}
    ans = data->rc;
    if (ans == false)
      break;
  }
  return ans;
}

static bool test_connect_worker(bool gzip, bool secure) {
  bool ans = false;
  uv_buf_t buf = nodec_buf_alloc(sizeof(struct PARAMS));
  {using_buf(&buf) {
    struct PARAMS* data = (struct PARAMS*)buf.base;
    data->rc = false;
    data->gzip = gzip;
    if (secure) {
      ans = test_secure_connection(data, SECURE_URLS, nodec_countof(SECURE_URLS));
    }
    else {
      ans = test_non_secure_connection(data, URLS, nodec_countof(URLS));
    }
  }}
  return ans;
}

TEST_IMPL(connect) {
  //                        gzip  secure
  CHECK(test_connect_worker(true, false));      // gzip, not-secure
  CHECK(test_connect_worker(true, true));       // gzip, secure
  //CHECK(test_connect_worker(false, false));   // throws an exception in async_shutdown
  //CHECK(test_connect_worker(false, true));    // throws an exception in async_shutdown
  TEST_IMPL_END;
}
