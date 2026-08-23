#ifndef XW_HARNESS_H
#define XW_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int xw_test_failures = 0;
static int xw_test_runs = 0;
static const char *xw_test_suite = "";

#define TEST_SUITE(name)                      \
  do {                                        \
    xw_test_suite = name;                     \
    printf("[%s]\n", name);                   \
  } while (0)

#define TEST_BEGIN(name)                      \
  do {                                        \
    xw_test_runs++;                           \
    printf("  - %s\n", name);                 \
  } while (0)

#define TEST_FAIL(...)                        \
  do {                                        \
    xw_test_failures++;                       \
    printf("    FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__);                      \
    printf("\n");                             \
  } while (0)

#define TEST_CHECK(cond)                      \
  do {                                        \
    if (!(cond))                              \
      TEST_FAIL("assertion failed: %s", #cond); \
  } while (0)

#define TEST_CHECK_EQ_INT(a, b)               \
  do {                                        \
    long long xa = (long long)(a);            \
    long long xb = (long long)(b);            \
    if (xa != xb)                             \
      TEST_FAIL("%s == %s (%lld != %lld)", #a, #b, xa, xb); \
  } while (0)

#define TEST_CHECK_EQ_STR(a, b)               \
  do {                                        \
    const char *sa = (a);                     \
    const char *sb = (b);                     \
    if (!sa || !sb || strcmp(sa, sb) != 0)    \
      TEST_FAIL("%s == %s (\"%s\" != \"%s\")", #a, #b, \
                sa ? sa : "(null)", sb ? sb : "(null)"); \
  } while (0)

#define TEST_SUMMARY()                        \
  do {                                        \
    printf("\n%d/%d tests passed\n",          \
           xw_test_runs - xw_test_failures, xw_test_runs); \
    return xw_test_failures == 0 ? 0 : 1;     \
  } while (0)

#endif /* XW_HARNESS_H */
