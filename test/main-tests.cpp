/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests++.h"

#include <iostream>

/*-----------------------------------------------------------------
testing
-----------------------------------------------------------------*/
int main(void) {
  std::cout << "testing C++: " << LH_CCNAME << ", " << LH_TARGET << std::endl;

  // regular C tests compiled as C++  
  test_exn();
  test_state();
  test_amb();
  
  test_dynamic();  
  test_raise();
  
  test_general();
  test_tailops();
  test_state_alloc();
  
  // C++ specific tests with destructors, finalizers etc.
  test_destructor();
  
  // Show stats
  tests_done();
  return 0;
}
