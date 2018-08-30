/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include <nodec.h>
#include "examples.h"


// Main entry point: choose your example here
static void entry() {
  //ex_http_server_static();
  ex_https_server_static();
}

int main() {
  async_main(entry);
  return 0;
}
