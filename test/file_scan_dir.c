#include <stdio.h>
#include <nodec.h>
#include "test.h"
#include <Windows.h>

static const uv_dirent_t recognized_entries[] = {
  { "nodec.crt", UV_DIRENT_FILE },
  { "nodec.key", UV_DIRENT_FILE },
};

static bool entry_recognized(const uv_dirent_t* entry) {
  for (int i = 0; i < nodec_countof(recognized_entries); i++) {
    const uv_dirent_t *expected = recognized_entries + i;
    if (strcmp(entry->name, expected->name) == 0) {
      if (entry->type == expected->type) {
        return true;
      }
    }
  }
  return false;
}

TEST_IMPL(scan_dir) {
  nodec_scandir_t* scan = async_fs_scandir("./examples/data");
  {using_fs_scandir(scan) {
    uv_dirent_t dirent;
    while (async_fs_scandir_next(scan, &dirent)) {
      nodec_log_debug("{ \"type\":%i, \"name\":\"%s\" }", dirent.type, dirent.name);
      CHECK(entry_recognized(&dirent));
    }
  }}

  TEST_IMPL_END;
}
