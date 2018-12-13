#include <stdio.h>
#include <nodec.h>
#include "test.h"

/*-----------------------------------------------------------------
  Test nested interleave
-----------------------------------------------------------------*/

static lh_value test_statx(lh_value arg) {
  TEST_NAME(file_stat)();
  return lh_value_null;
}
static lh_value test_filereadx(lh_value arg) {
  TEST_NAME(file_read)();
  return lh_value_null;
}
static lh_value test_filereads(lh_value arg) {
  lh_actionfun* actions[2] = { &test_filereadx, &test_statx };
  async_interleave(2, actions, NULL);
  return lh_value_null;
}

TEST_IMPL(file_interleave) {
  lh_actionfun* actions[3] = { &test_filereadx, &test_statx, &test_filereads };
  async_interleave(3, actions, NULL);
  TEST_IMPL_END;
}
