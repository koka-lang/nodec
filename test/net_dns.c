#include <stdio.h>
#include <nodec.h>
#include "test.h"

TEST_IMPL(dns) {
  struct addrinfo* info = async_getaddrinfo("iana.org", NULL, NULL);
  CHECK(info != NULL);
  {using_addrinfo(info) {
    for (struct addrinfo* current = info; current != NULL; current = current->ai_next) {
      char sockname[128];
      nodec_sockname(current->ai_addr, sockname, sizeof(sockname));
      char* host = NULL;
      async_getnameinfo(current->ai_addr, 0, &host, NULL);
      {using_free(host) {
        nodec_log_debug("info: protocol %i at %s, reverse host: %s", current->ai_protocol, sockname, host);
      }}
    }
  }}
  TEST_IMPL_END;
}
