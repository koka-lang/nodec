#include <stdio.h>
#include <nodec.h>
#include "test.h"


/*-----------------------------------------------------------------
 test file reading
-----------------------------------------------------------------*/
static const char* crt =
"-----BEGIN CERTIFICATE-----\n"
"MIIDsTCCApmgAwIBAgIJAJ25oPl2dHMYMA0GCSqGSIb3DQEBCwUAMG8xCzAJBgNV\n"
"BAYTAlVTMRMwEQYDVQQIDApXYXNoaW5ndG9uMRAwDgYDVQQHDAdSZWRtb25kMRIw\n"
"EAYDVQQKDAlNaWNyb3NvZnQxETAPBgNVBAsMCFJlc2VhcmNoMRIwEAYDVQQDDAls\n"
"b2NhbGhvc3QwHhcNMTgwODA5MTUxMTQ4WhcNMTkwODA5MTUxMTQ4WjBvMQswCQYD\n"
"VQQGEwJVUzETMBEGA1UECAwKV2FzaGluZ3RvbjEQMA4GA1UEBwwHUmVkbW9uZDES\n"
"MBAGA1UECgwJTWljcm9zb2Z0MREwDwYDVQQLDAhSZXNlYXJjaDESMBAGA1UEAwwJ\n"
"bG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAtMWohvqt\n"
"lS2LWPzzWEbVocbTCkts1JbVxw1dSbxunzDpROeAIhcrhoady5AQ0OGib4OBxXK+\n"
"aPrfQuFPggM9T1XSGfo5oRgz5jZtcl+WYKzB//t7ZP1/mZFPPIs7rOlX3ToYXhe5\n"
"0Y/dcGA90RQsrV+zUua62JXRmh3yDniO/BctJbbCkpsXhukXzgthhlQt8TOuY9Tz\n"
"LXvUzD0SEW0ejFyVuRu9q7EdERJ+siA0LV7XKK2ocClrlagNJMqShV63BxjUBAYb\n"
"Ck7ioucADTnbnbmSlU7xCyrv6sZ/isnV2KToiWsh9XTgEcmvwsO4I8PumYCij2Z4\n"
"C3JGCDUGM892CQIDAQABo1AwTjAdBgNVHQ4EFgQUyb2qtTsL+LHv6JsZHpsUjTDe\n"
"ExcwHwYDVR0jBBgwFoAUyb2qtTsL+LHv6JsZHpsUjTDeExcwDAYDVR0TBAUwAwEB\n"
"/zANBgkqhkiG9w0BAQsFAAOCAQEAcVdPHp3xeWz9ufq/Cf8JK3XXykZBA1mreTGI\n"
"g3iaH17PZf+wA8fXRr3tkgvE2E+/jTz9hHUiYcMIFS+w4HqYsgJgpXA+LYcOL2RZ\n"
"SJX8BVj7MJpwzRW81+yVfNFOkj6FdCXxV/JNv77Xl4jK0FnsCs1903aJP+bnwirK\n"
"h14nWMXX4sqiNS823UuESQBBdRgeTUeVvZcF0LjX+Ql4RhfBtET21GsxyukchpxM\n"
"7ZNIruaBulHwDjakAwoOsxTXwbCxiVRKQGCun8D/ZykRKXigyShN2Dxzg20exyQw\n"
"2dqAUf7X78h7Md5wz5OaUJkLCfjcePOeVmFQGFIDI85BEJfABw==\n"
"-----END CERTIFICATE-----\n";

TEST_IMPL(file_read) {
  const char* path = "./examples/data/nodec.crt";
  nodec_log_debug("opening file: %s.", path);
  char* contents = async_fs_read_from(path);
  {using_free(contents) {
    nodec_log_debug("read %zi bytes from %s.", strlen(contents), path);
    CHECK(strcmp(contents, crt) == 0);
  }}
  TEST_IMPL_END;
}

