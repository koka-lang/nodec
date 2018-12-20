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
  CHECK(stat.st_mode == 33206);
  CHECK(stat.st_size == 1342);
  TEST_IMPL_END;
}

