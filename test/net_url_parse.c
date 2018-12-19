#include <stdio.h>
#include <nodec.h>
#include "test.h"


/*-----------------------------------------------------------------
 test url parsing
-----------------------------------------------------------------*/

static bool url_check(const char* urlstr, const char* expected) {
  nodec_url_t* url = nodec_parse_url(urlstr);
  bool ok;
  {using_url(url) {
    char buf[256];
    snprintf(buf, 255, "schema: %s\n ui: %s\n host: %s\n port: %u\n path: %s\n query: %s\n fragment: %s\n",
      nodec_url_schema(url), nodec_url_userinfo(url), nodec_url_host(url),
      nodec_url_port(url),
      nodec_url_path(url), nodec_url_query(url), nodec_url_fragment(url)
    );
    ok = strcmp(buf, expected) == 0;
  }}
  return ok;
}

static bool host_url_check(const char* urlstr, const char* expected) {
  nodec_url_t* url = nodec_parse_host(urlstr);
  bool ok;
  {using_url(url) {
    char buf[256];
    snprintf(buf, 255, "host: %s, port: %u",
      nodec_url_host(url), nodec_url_port(url)
    );
    ok = strcmp(buf, expected) == 0;
  }}
  return ok;
}

TEST_IMPL(url_parse) {
  CHECK(url_check("http://daan@www.bing.com:72/foo?x=10;y=3#locallink", "schema: http\n ui: daan\n host: www.bing.com\n port: 72\n path: foo\n query: x=10;y=3\n fragment: locallink\n"));
  CHECK(url_check("https://bing.com:8080", "schema: https\n ui: (null)\n host: bing.com\n port: 8080\n path: (null)\n query: (null)\n fragment: (null)\n"));
  CHECK(url_check("http://127.0.0.1", "schema: http\n ui: (null)\n host: 127.0.0.1\n port: 0\n path: (null)\n query: (null)\n fragment: (null)\n"));
  CHECK(host_url_check("localhost:8080", "host: localhost, port: 8080"));
  CHECK(host_url_check("my.server.com:80", "host: my.server.com, port: 80"));
  CHECK(host_url_check("127.0.0.1:80", "host: 127.0.0.1, port: 80"));
  CHECK(host_url_check("[2001:db8:85a3:8d3:1319:8a2e:370:7348]:443", "host: 2001:db8:85a3:8d3:1319:8a2e:370:7348, port: 443"));
  //host_url_print("http://127.0.0.1"); // invalid
  //host_url_print("127.0.0.1");        // invalid
  TEST_IMPL_END;
}