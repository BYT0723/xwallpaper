#include "harness.h"
#include "../src/web_dir_scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char root[512];

static void put(const char *rel) {
  char path[700];
  snprintf(path, sizeof(path), "%s/%s", root, rel);
  FILE *f = fopen(path, "w");
  if (f) {
    fputs("x", f);
    fclose(f);
  }
}

static void test_match(void) {
  TEST_BEGIN("directory scan filters by file type");
  TEST_CHECK(xw_web_dir_match("a.PNG", "image"));
  TEST_CHECK(xw_web_dir_match("a.jpeg", "image"));
  TEST_CHECK(xw_web_dir_match("a.webp", "image"));
  TEST_CHECK(!xw_web_dir_match("a.webm", "image"));
  TEST_CHECK(!xw_web_dir_match("a.txt", ""));
  TEST_CHECK(xw_web_dir_match("a.webm", "video"));
  TEST_CHECK(xw_web_dir_match("a.OGV", "video"));
  TEST_CHECK(!xw_web_dir_match("a.png", "video"));
}

static void test_scan(void) {
  char **files = NULL;
  int n;
  TEST_BEGIN("directory scan recurses, sorts and caps");
  n = xw_web_dir_scan(root, "image", &files, 64);
  TEST_CHECK_EQ_INT(n, 3);
  if (n == 3) {
    TEST_CHECK(strstr(files[0], "/a.png") != NULL);
    TEST_CHECK(strstr(files[1], "/b.jpg") != NULL);
    TEST_CHECK(strstr(files[2], "/sub/c.webp") != NULL);
  }
  for (int i = 0; i < n; i++)
    free(files[i]);
  free(files);
  files = NULL;

  n = xw_web_dir_scan(root, "video", &files, 64);
  TEST_CHECK_EQ_INT(n, 1);
  if (n == 1)
    TEST_CHECK(strstr(files[0], "/d.webm") != NULL);
  for (int i = 0; i < n; i++)
    free(files[i]);
  free(files);
  files = NULL;

  /* cap: newest files are not guaranteed, only the count is bounded */
  n = xw_web_dir_scan(root, "image", &files, 2);
  TEST_CHECK_EQ_INT(n, 2);
  for (int i = 0; i < n; i++)
    free(files[i]);
  free(files);

  TEST_CHECK_EQ_INT(xw_web_dir_scan("/no/such/dir", "image", &files, 8), -1);
  TEST_CHECK_EQ_INT(xw_web_dir_scan(NULL, "image", &files, 8), -1);
}

static void test_diff(void) {
  char *prev[] = {(char *)"a.png", (char *)"b.png", (char *)"d.png"};
  char *cur[] = {(char *)"b.png", (char *)"c.png", (char *)"d.png",
                 (char *)"e.png"};
  char **add = NULL, **rem = NULL;
  int na = -1, nr = -1, i;
  TEST_BEGIN("directory diff reports added and removed paths");
  TEST_CHECK_EQ_INT(xw_web_dir_diff(prev, 3, cur, 4, &add, &na, &rem, &nr), 0);
  TEST_CHECK_EQ_INT(na, 2);
  TEST_CHECK_EQ_INT(nr, 1);
  if (na == 2 && add) {
    TEST_CHECK_EQ_STR(add[0], "c.png");
    TEST_CHECK_EQ_STR(add[1], "e.png");
  }
  if (nr == 1 && rem)
    TEST_CHECK_EQ_STR(rem[0], "a.png");
  for (i = 0; i < na; i++)
    free(add[i]);
  free(add);
  for (i = 0; i < nr; i++)
    free(rem[i]);
  free(rem);

  add = rem = NULL;
  TEST_CHECK_EQ_INT(xw_web_dir_diff(prev, 3, prev, 3, &add, &na, &rem, &nr),
                    0);
  TEST_CHECK_EQ_INT(na, 0);
  TEST_CHECK_EQ_INT(nr, 0);
  TEST_CHECK(add == NULL && rem == NULL);

  add = rem = NULL;
  TEST_CHECK_EQ_INT(xw_web_dir_diff(NULL, 0, cur, 4, &add, &na, &rem, &nr),
                    0);
  TEST_CHECK_EQ_INT(na, 4);
  TEST_CHECK_EQ_INT(nr, 0);
  for (i = 0; i < na; i++)
    free(add[i]);
  free(add);

  add = rem = NULL;
  TEST_CHECK_EQ_INT(xw_web_dir_diff(prev, 3, NULL, 0, &add, &na, &rem, &nr),
                    0);
  TEST_CHECK_EQ_INT(na, 0);
  TEST_CHECK_EQ_INT(nr, 3);
  for (i = 0; i < nr; i++)
    free(rem[i]);
  free(rem);

  TEST_CHECK_EQ_INT(xw_web_dir_diff(NULL, 0, NULL, 0, NULL, &na, NULL, &nr),
                    -1);
  TEST_CHECK_EQ_INT(xw_web_dir_diff(prev, 3, cur, 4, &add, NULL, &rem, &nr),
                    -1);
}

int main(void) {
  char tmpl[] = "/tmp/opencode/xwscan-XXXXXX";
  char sub[600], leak[700];
  TEST_SUITE("web directory scan (no X)");
  TEST_CHECK(mkdtemp(tmpl) != NULL);
  snprintf(root, sizeof(root), "%s", tmpl);
  snprintf(sub, sizeof(sub), "%s/sub", root);
  mkdir(sub, 0755);
  put("a.png");
  put("b.jpg");
  put("d.webm");
  put("skip.txt");
  put("sub/c.webp");
  /* symlinks are never followed: file links are skipped, dir loops too */
  snprintf(leak, sizeof(leak), "%s/link.png", root);
  symlink("/etc/passwd", leak);
  snprintf(leak, sizeof(leak), "%s/sub/loop", root);
  symlink(root, leak);
  test_match();
  test_scan();
  test_diff();
  TEST_SUMMARY();
}
