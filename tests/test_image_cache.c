/* Unit tests: refcounted decoded-image cache (path + mtime + size key).
 * Fake loader/freer keep this free of imlib2, X and Xvfb. */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"
#include "../src/image_cache.h"

typedef struct {
  int loads;
  int frees;
} Fake;

static void *fake_load(void *ud, const char *path) {
  Fake *f = (Fake *)ud;
  f->loads++;
  if (strstr(path, "broken"))
    return NULL;
  return strdup("decoded");
}

static void fake_free(void *ud, void *img) {
  Fake *f = (Fake *)ud;
  char *p = (char *)img;
  TEST_CHECK_EQ_STR(p, "decoded");
  f->frees++;
  free(img);
}

static char root[256];

static void write_file(const char *name, const char *body) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", root, name);
  FILE *f = fopen(path, "wb");
  if (!f) {
    TEST_FAIL("cannot create %s", path);
    return;
  }
  fwrite(body, 1, strlen(body), f);
  fclose(f);
}

static void append_file(const char *name, const char *extra) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", root, name);
  FILE *f = fopen(path, "ab");
  if (!f) {
    TEST_FAIL("cannot append %s", path);
    return;
  }
  fwrite(extra, 1, strlen(extra), f);
  fclose(f);
}

static void bump_mtime(const char *name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", root, name);
  struct timespec ts[2];
  clock_gettime(CLOCK_REALTIME, &ts[0]);
  ts[1] = ts[0];
  ts[0].tv_sec += 60;
  ts[1].tv_sec += 60;
  if (utimensat(AT_FDCWD, path, ts, 0) != 0)
    TEST_FAIL("cannot touch %s", path);
}

static XWImageCache *make_cache(Fake *f) {
  XWImageCacheOps ops = {.load = fake_load, .free = fake_free, .ud = f};
  return xw_image_cache_new(&ops);
}

static void test_shared_between_windows(void) {
  Fake f = {0};
  XWImageCache *c = make_cache(&f);
  char path[512];

  TEST_BEGIN("two references to one file decode it once");
  write_file("shared.png", "a");
  snprintf(path, sizeof(path), "%s/shared.png", root);
  XWImageRef *a = xw_image_cache_get(c, path);
  XWImageRef *b = xw_image_cache_get(c, path);
  TEST_CHECK(a != NULL && b != NULL);
  TEST_CHECK(a != b);
  TEST_CHECK(xw_image_ref_image(a) == xw_image_ref_image(b));
  TEST_CHECK_EQ_INT(f.loads, 1);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(c), 1);
  TEST_CHECK_EQ_INT(xw_image_cache_refs(c, path), 2);

  TEST_BEGIN("first put keeps the image alive for the second holder");
  xw_image_cache_put(c, a);
  TEST_CHECK_EQ_INT(f.frees, 0);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(c), 1);
  TEST_CHECK_EQ_STR((char *)xw_image_ref_image(b), "decoded");

  TEST_BEGIN("last put frees the decoded image");
  xw_image_cache_put(c, b);
  TEST_CHECK_EQ_INT(f.frees, 1);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(c), 0);

  TEST_BEGIN("re-acquiring after release decodes again (no idle cache)");
  XWImageRef *again = xw_image_cache_get(c, path);
  TEST_CHECK(again != NULL);
  TEST_CHECK_EQ_INT(f.loads, 2);
  xw_image_cache_put(c, again);
  TEST_CHECK_EQ_INT(f.frees, 2);
  xw_image_cache_free(c);
}

static void test_path_spellings(void) {
  Fake f = {0};
  XWImageCache *c = make_cache(&f);
  char abs[512], rel[512], dotted[512];

  TEST_BEGIN("one file reached by different spellings shares one decode");
  write_file("spell.png", "a");
  snprintf(abs, sizeof(abs), "%s/spell.png", root);
  snprintf(rel, sizeof(rel), "%s/spell.png", root);
  snprintf(dotted, sizeof(dotted), "%s/./spell.png", root);
  XWImageRef *a = xw_image_cache_get(c, abs);
  XWImageRef *b = xw_image_cache_get(c, dotted);
  XWImageRef *d = xw_image_cache_get(c, rel);
  TEST_CHECK(a && b && d);
  TEST_CHECK_EQ_INT(f.loads, 1);
  TEST_CHECK_EQ_INT(xw_image_cache_refs(c, dotted), 3);
  xw_image_cache_put(c, a);
  xw_image_cache_put(c, b);
  xw_image_cache_put(c, d);
  TEST_CHECK_EQ_INT(f.frees, 1);
  xw_image_cache_free(c);
}

static void test_changed_file(void) {
  Fake f = {0};
  XWImageCache *c = make_cache(&f);
  char path[512];

  TEST_BEGIN("same size but new mtime decodes a fresh entry");
  write_file("changed.png", "aa");
  snprintf(path, sizeof(path), "%s/changed.png", root);
  XWImageRef *a = xw_image_cache_get(c, path);
  bump_mtime("changed.png");
  XWImageRef *b = xw_image_cache_get(c, path);
  TEST_CHECK(a && b);
  TEST_CHECK_EQ_INT(f.loads, 2);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(c), 2);

  TEST_BEGIN("the superseded entry stays valid until its holder drops it");
  TEST_CHECK_EQ_STR((char *)xw_image_ref_image(a), "decoded");
  TEST_CHECK_EQ_INT(f.frees, 0);
  xw_image_cache_put(c, a);
  TEST_CHECK_EQ_INT(f.frees, 1);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(c), 1);
  xw_image_cache_put(c, b);
  TEST_CHECK_EQ_INT(f.frees, 2);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(c), 0);

  TEST_BEGIN("a size change alone also invalidates");
  write_file("grown.png", "a");
  snprintf(path, sizeof(path), "%s/grown.png", root);
  XWImageRef *g1 = xw_image_cache_get(c, path);
  append_file("grown.png", "b");
  XWImageRef *g2 = xw_image_cache_get(c, path);
  TEST_CHECK(g1 && g2);
  TEST_CHECK_EQ_INT(f.loads, 4);
  xw_image_cache_put(c, g1);
  xw_image_cache_put(c, g2);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(c), 0);
  xw_image_cache_free(c);
}

static void test_failures(void) {
  Fake f = {0};
  XWImageCache *c = make_cache(&f);
  char broken[512], gone[512];

  TEST_BEGIN("loader failure yields NULL and caches nothing");
  write_file("broken.png", "a");
  snprintf(broken, sizeof(broken), "%s/broken.png", root);
  TEST_CHECK(xw_image_cache_get(c, broken) == NULL);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(c), 0);
  TEST_CHECK_EQ_INT(xw_image_cache_refs(c, broken), 0);

  TEST_BEGIN("a missing file is not handed to the loader");
  snprintf(gone, sizeof(gone), "%s/gone.png", root);
  TEST_CHECK(xw_image_cache_get(c, gone) == NULL);
  TEST_CHECK_EQ_INT(f.loads, 1);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(c), 0);

  TEST_BEGIN("NULL arguments are tolerated");
  TEST_CHECK(xw_image_cache_get(c, NULL) == NULL);
  TEST_CHECK(xw_image_cache_get(NULL, broken) == NULL);
  xw_image_cache_put(c, NULL);
  xw_image_cache_put(NULL, NULL);
  TEST_CHECK_EQ_INT(xw_image_ref_image(NULL), 0);
  TEST_CHECK_EQ_INT(xw_image_cache_entries(NULL), 0);
  xw_image_cache_free(NULL);
  xw_image_cache_free(c);
}

int main(void) {
  snprintf(root, sizeof(root), "/tmp/xw-imgcache-XXXXXX");
  if (!mkdtemp(root)) {
    printf("cannot create temp dir\n");
    return 1;
  }
  TEST_SUITE("image_cache");
  test_shared_between_windows();
  test_path_spellings();
  test_changed_file();
  test_failures();
  TEST_SUMMARY();
}
