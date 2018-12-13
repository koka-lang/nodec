#include <stdio.h>
#include <nodec.h>
#include "test.h"


/*-----------------------------------------------------------------
 test file stat
-----------------------------------------------------------------*/

TEST_IMPL(file_stat) {
  const char* path = "./examples/data/nodec.crt";
  nodec_log_debug("stat file %s", path);
  uv_stat_t stat = async_fs_stat(path);
  nodec_log_debug("file %s last access time: %li", path, stat.st_atim.tv_sec);
  CHECK(stat.st_atim.tv_sec == 1544725187L);
  TEST_IMPL_END;
}

