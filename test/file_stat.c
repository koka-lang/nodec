#include <stdio.h>
#include <nodec.h>
#include "test.h"


/*-----------------------------------------------------------------
 test file stat
-----------------------------------------------------------------*/

typedef struct _expected {
  const char* path;
  uint64_t mode;
  uint64_t size;
} expected_t;

static expected_t const EXPECTED[] = {
  //--------- name ----------    mode  size
  {"./examples/data/nodec.crt", 33206, 1342},
  {"./examples/data/nodec.key", 33206, 1766},
};

static bool check_expected(const expected_t* const expt) {
  nodec_log_debug("stat file %s", expt->path);
  uv_stat_t stat = async_fs_stat(expt->path);
  if (stat.st_mode != expt->mode)
    return false;
  if (stat.st_size != expt->size)
    return false;
  return true;
}

TEST_IMPL(file_stat) {
  for (size_t i = 0; i < nodec_countof(EXPECTED); i++) {
    CHECK(check_expected(EXPECTED + i));
  }
  TEST_IMPL_END;
}

