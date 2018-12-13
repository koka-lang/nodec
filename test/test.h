#pragma once
#ifndef __TEST_H
#define __TEST_H

/*-----------------------------------------------------------------
  Declare all tests
-----------------------------------------------------------------*/
#define TEST_ENTRY(name)                TEST_ENTRYX(name,false)
#define TEST_ENTRYX(name,showoutput)    /*empty for now*/

#define TEST_LIST \
  TEST_ENTRY(url_parse) \
  TEST_ENTRY(file_read) \
  TEST_ENTRY(file_stat) \
  TEST_ENTRY(file_interleave) \


/*-----------------------------------------------------------------
  Helpers
-----------------------------------------------------------------*/

typedef enum test_result_e {
  RES_OK = 0,
  RES_SKIP = 1,
  RES_FAIL = 2
} test_result_t;


typedef struct test_info_s {
  const char* name;
  int       (*fun)();
  bool        show_output;
} test_info_t;


#define CHECK(expr)                                      \
 do {                                                     \
  if (!(expr)) {                                          \
    fprintf(stderr,                                       \
            "assertion failed in %s on line %d:\n  %s\n", \
            __FILE__,                                     \
            __LINE__,                                     \
            #expr);                                       \
    goto test_fail;                                       \
  }                                                       \
 } while (0)



/*-----------------------------------------------------------------
  Test function declarations
-----------------------------------------------------------------*/
#define TEST_OK()                     return RES_OK
#define TEST_SKIP(explanation)        do{ fprintf(stderr,explanation); return RES_SKIP; } while(0)


#define TEST_NAME(name)               test_##name
#define TEST_FUN(name)                int TEST_NAME(name)() 
#define TEST_IMPL(name)               TEST_FUN(name)
#define TEST_IMPL_END                 TEST_OK(); test_fail: return RES_FAIL;

#define TEST_ENTRYX(name,showoutput)  TEST_IMPL(name);
TEST_LIST


#endif