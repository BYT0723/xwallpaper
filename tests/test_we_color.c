/* Unit tests: album-art palette extraction and contrast text colors.
 * Pure C: no GLib, GTK, X or Xvfb. */

#include <string.h>

#include "harness.h"
#include "../src/we_color.h"

static void fill_solid(unsigned char *px, int w, int h, int nch, unsigned r,
                       unsigned g, unsigned b, unsigned a) {
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      unsigned char *p = px + ((size_t)y * w + x) * (size_t)nch;
      p[0] = (unsigned char)r;
      p[1] = (unsigned char)g;
      p[2] = (unsigned char)b;
      if (nch == 4)
        p[3] = (unsigned char)a;
    }
  }
}

static void palette(const unsigned char *px, int w, int h, int rowstride,
                    int nch, char p[8], char s[8], char t[8], char tx[8],
                    char hi[8]) {
  TEST_CHECK_EQ_INT(
      xw_we_color_palette(px, w, h, rowstride, nch, p, s, t, tx, hi), 0);
}

static void test_solid_colors(void) {
  char p[8], s[8], t[8], tx[8], hi[8];

  TEST_BEGIN("1x1 solid red yields #ff0000 and its best contrast text");
  unsigned char one[4] = {255, 0, 0, 255};
  palette(one, 1, 1, 4, 4, p, s, t, tx, hi);
  TEST_CHECK_EQ_STR(p, "#ff0000");
  TEST_CHECK_EQ_STR(s, "");
  TEST_CHECK_EQ_STR(t, "");
  /* WCAG contrast on pure red: black is 5.25:1, white only 4.0:1. */
  TEST_CHECK_EQ_STR(tx, "#000000");
  TEST_CHECK_EQ_STR(hi, "#000000");

  TEST_BEGIN("8x8 solid red without alpha also yields one cluster");
  unsigned char red[8 * 8 * 3];
  fill_solid(red, 8, 8, 3, 255, 0, 0, 0);
  palette(red, 8, 8, 8 * 3, 3, p, s, t, tx, hi);
  TEST_CHECK_EQ_STR(p, "#ff0000");
  TEST_CHECK_EQ_STR(s, "");
  TEST_CHECK_EQ_STR(tx, "#000000");

  TEST_BEGIN("solid black picks white text");
  unsigned char black[8 * 8 * 3];
  fill_solid(black, 8, 8, 3, 0, 0, 0, 0);
  palette(black, 8, 8, 8 * 3, 3, p, s, t, tx, hi);
  TEST_CHECK_EQ_STR(p, "#000000");
  TEST_CHECK_EQ_STR(tx, "#ffffff");
  TEST_CHECK_EQ_STR(hi, "#ffffff");

  TEST_BEGIN("dark navy picks white text");
  unsigned char navy[4] = {0, 0, 128, 255};
  palette(navy, 1, 1, 4, 4, p, s, t, tx, hi);
  TEST_CHECK_EQ_STR(p, "#000080");
  TEST_CHECK_EQ_STR(tx, "#ffffff");
}

static void test_transparency(void) {
  char p[8] = "junk", s[8] = "junk", t[8] = "junk", tx[8] = "junk",
       hi[8] = "junk";

  TEST_BEGIN("fully transparent pixels produce no colors at all");
  unsigned char px[4 * 4 * 4];
  memset(px, 0, sizeof(px));
  palette(px, 4, 4, 4 * 4, 4, p, s, t, tx, hi);
  TEST_CHECK_EQ_STR(p, "");
  TEST_CHECK_EQ_STR(s, "");
  TEST_CHECK_EQ_STR(t, "");
  TEST_CHECK_EQ_STR(tx, "");
  TEST_CHECK_EQ_STR(hi, "");
}

static void test_distinct_clusters(void) {
  char p[8], s[8], t[8], tx[8], hi[8];

  TEST_BEGIN("two colors: primary is the larger cluster, secondary differs");
  unsigned char two[4 * 2 * 3];
  fill_solid(two, 4, 2, 3, 255, 0, 0, 0);
  /* Last two pixels blue: 6 red vs 2 blue. */
  two[6 * 3 + 0] = 0;
  two[6 * 3 + 1] = 0;
  two[6 * 3 + 2] = 255;
  two[7 * 3 + 0] = 0;
  two[7 * 3 + 1] = 0;
  two[7 * 3 + 2] = 255;
  palette(two, 4, 2, 4 * 3, 3, p, s, t, tx, hi);
  TEST_CHECK_EQ_STR(p, "#ff0000");
  TEST_CHECK_EQ_STR(s, "#0000ff");
  TEST_CHECK_EQ_STR(t, "");

  TEST_BEGIN("three colors fill primary/secondary/tertiary");
  unsigned char three[9 * 3];
  for (int i = 0; i < 4; i++) {
    three[i * 3 + 0] = 255;
    three[i * 3 + 1] = 0;
    three[i * 3 + 2] = 0;
  }
  for (int i = 4; i < 7; i++) {
    three[i * 3 + 0] = 0;
    three[i * 3 + 1] = 255;
    three[i * 3 + 2] = 0;
  }
  for (int i = 7; i < 9; i++) {
    three[i * 3 + 0] = 0;
    three[i * 3 + 1] = 0;
    three[i * 3 + 2] = 255;
  }
  palette(three, 9, 1, 9 * 3, 3, p, s, t, tx, hi);
  TEST_CHECK_EQ_STR(p, "#ff0000");
  TEST_CHECK_EQ_STR(s, "#00ff00");
  TEST_CHECK_EQ_STR(t, "#0000ff");

  TEST_BEGIN("near-identical colors do not become a second cluster");
  unsigned char near[4 * 3];
  for (int i = 0; i < 3; i++) {
    near[i * 3 + 0] = 255;
    near[i * 3 + 1] = 0;
    near[i * 3 + 2] = 0;
  }
  near[3 * 3 + 0] = 230;
  near[3 * 3 + 1] = 0;
  near[3 * 3 + 2] = 0;
  palette(near, 4, 1, 4 * 3, 3, p, s, t, tx, hi);
  TEST_CHECK_EQ_STR(p, "#ff0000");
  TEST_CHECK_EQ_STR(s, "");
}

static void test_rowstride_padding(void) {
  char p[8], s[8], t[8], tx[8], hi[8];

  TEST_BEGIN("rowstride padding bytes are not sampled as pixels");
  unsigned char px[2 * 8];
  memset(px, 0, sizeof(px));
  for (int y = 0; y < 2; y++) {
    unsigned char *row = px + y * 8;
    row[0] = 255; /* red pixel */
    row[1] = 0;
    row[2] = 0;
    row[3] = 255; /* red pixel */
    row[4] = 0;
    row[5] = 0;
    row[6] = 0; /* padding: would be blue if read as a pixel */
    row[7] = 255;
  }
  palette(px, 2, 2, 8, 3, p, s, t, tx, hi);
  TEST_CHECK_EQ_STR(p, "#ff0000");
  TEST_CHECK_EQ_STR(s, "");
  TEST_CHECK_EQ_STR(t, "");
}

static void test_bad_args(void) {
  char p[8], s[8], t[8], tx[8], hi[8];
  unsigned char px[4] = {255, 0, 0, 255};

  TEST_BEGIN("bad arguments are rejected");
  TEST_CHECK_EQ_INT(xw_we_color_palette(NULL, 1, 1, 4, 4, p, s, t, tx, hi),
                    -1);
  TEST_CHECK_EQ_INT(xw_we_color_palette(px, 0, 1, 4, 4, p, s, t, tx, hi), -1);
  TEST_CHECK_EQ_INT(xw_we_color_palette(px, 1, 1, 4, 2, p, s, t, tx, hi), -1);
  TEST_CHECK_EQ_INT(xw_we_color_palette(px, 1, 1, 4, 4, NULL, s, t, tx, hi),
                    -1);
}

int main(void) {
  TEST_SUITE("we_color");
  test_solid_colors();
  test_transparency();
  test_distinct_clusters();
  test_rowstride_padding();
  test_bad_args();
  TEST_SUMMARY();
}
