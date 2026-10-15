#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "../src/scene_mesh.h"

/* Build a synthetic MDLV container: 9-byte marker, then zero padding, then
 * one mesh block (8-byte header + vertices + u32 index bytes + indices),
 * then the MDLS marker that terminates the scan. */
static unsigned char *build_mdlv(const char *marker, unsigned int n_verts,
                                 unsigned int n_idx, size_t *out_len) {
  const size_t stride = 80, hdr = 8, marker_size = 9;
  const size_t pad = 7; /* zero bytes the scanner must reject */
  size_t vertex_bytes = (size_t)n_verts * stride;
  size_t index_bytes = (size_t)n_idx * sizeof(unsigned short);
  size_t mdls_at = marker_size + pad + hdr + vertex_bytes + 4 + index_bytes;
  size_t total = mdls_at + 4;
  unsigned char *b = calloc(1, total);
  unsigned int i;
  if (!b)
    return NULL;
  memset(b, 'M', marker_size);
  memcpy(b, marker, strlen(marker));
  b[marker_size - 1] = '\0';
  /* block header: [u32 unknown][u32 vertexBytes] */
  b[marker_size + pad + 4] = (unsigned char)(vertex_bytes & 0xff);
  b[marker_size + pad + 5] = (unsigned char)((vertex_bytes >> 8) & 0xff);
  b[marker_size + pad + 6] = (unsigned char)((vertex_bytes >> 16) & 0xff);
  b[marker_size + pad + 7] = (unsigned char)((vertex_bytes >> 24) & 0xff);
  for (i = 0; i < n_verts; i++) {
    unsigned char *v = b + marker_size + pad + hdr + (size_t)i * stride;
    float px = (float)i, py = (float)i + 0.5f, pz = 0.0f;
    float u = (float)i * 0.25f, vv = 1.0f - (float)i * 0.25f;
    memcpy(v + 0, &px, 4);
    memcpy(v + 4, &py, 4);
    memcpy(v + 8, &pz, 4);
    memcpy(v + 72, &u, 4);
    memcpy(v + 76, &vv, 4);
  }
  {
    size_t at = marker_size + pad + hdr + vertex_bytes;
    b[at] = (unsigned char)(index_bytes & 0xff);
    b[at + 1] = (unsigned char)((index_bytes >> 8) & 0xff);
    b[at + 2] = (unsigned char)((index_bytes >> 16) & 0xff);
    b[at + 3] = (unsigned char)((index_bytes >> 24) & 0xff);
    for (i = 0; i < n_idx; i++) {
      unsigned short ix = (unsigned short)i;
      memcpy(b + at + 4 + (size_t)i * 2, &ix, 2);
    }
  }
  memcpy(b + mdls_at, "MDLS", 4);
  *out_len = total;
  return b;
}

static void test_parse_mesh(void) {
  XWError err = {0};
  XWMesh m;
  size_t len = 0;
  unsigned char *b = build_mdlv("MDLV0023", 4, 6, &len);
  TEST_BEGIN("MDLV0023 mesh: vertices, uvs and indices");
  TEST_CHECK(b != NULL);
  if (!b)
    return;
  memset(&m, 0, sizeof(m));
  TEST_CHECK_EQ_INT(xw_mesh_parse(b, len, &m, &err), 0);
  TEST_CHECK_EQ_INT((int)m.n_vertices, 4);
  TEST_CHECK_EQ_INT((int)m.n_indices, 6);
  if (m.n_vertices == 4 && m.n_indices == 6) {
    /* position at offset 0, uv at offset 72 */
    TEST_CHECK(m.pos[0] > -0.001f && m.pos[0] < 0.001f);
    TEST_CHECK(m.pos[1] > 0.499f && m.pos[1] < 0.501f);
    TEST_CHECK(m.pos[9] > 2.999f && m.pos[9] < 3.001f);
    TEST_CHECK(m.uv[0] > -0.001f && m.uv[0] < 0.001f);
    TEST_CHECK(m.uv[1] > 0.999f && m.uv[1] < 1.001f);
    TEST_CHECK(m.uv[2] > 0.249f && m.uv[2] < 0.251f);
    TEST_CHECK_EQ_INT((int)m.idx[0], 0);
    TEST_CHECK_EQ_INT((int)m.idx[5], 5);
  }
  xw_mesh_free(&m);
  free(b);
}

static void test_parse_mdlv0021(void) {
  XWError err = {0};
  XWMesh m;
  size_t len = 0;
  unsigned char *b = build_mdlv("MDLV0021", 3, 3, &len);
  TEST_BEGIN("MDLV0021 is accepted too");
  TEST_CHECK(b != NULL);
  if (!b)
    return;
  memset(&m, 0, sizeof(m));
  TEST_CHECK_EQ_INT(xw_mesh_parse(b, len, &m, &err), 0);
  TEST_CHECK_EQ_INT((int)m.n_vertices, 3);
  xw_mesh_free(&m);
  free(b);
}

static void test_rejects(void) {
  XWError err = {0};
  XWMesh m;
  size_t len = 0;
  unsigned char *b;
  /* MDLV0016 is the version the engine itself refuses */
  b = build_mdlv("MDLV0016", 4, 6, &len);
  TEST_BEGIN("MDLV0016 and malformed containers are refused");
  TEST_CHECK(b != NULL);
  if (b) {
    memset(&m, 0, sizeof(m));
    TEST_CHECK_EQ_INT(xw_mesh_parse(b, len, &m, &err), -1);
    TEST_CHECK(m.n_vertices == 0 && m.idx == NULL);
    free(b);
  }
  /* no MDLS terminator: the block scan would run past the end */
  b = calloc(1, 64);
  if (b) {
    memcpy(b, "MDLV0023", 8);
    memset(&m, 0, sizeof(m));
    TEST_CHECK_EQ_INT(xw_mesh_parse(b, 64, &m, &err), -1);
    free(b);
  }
  /* too short to hold a marker */
  TEST_CHECK_EQ_INT(xw_mesh_parse((const unsigned char *)"MDLV00", 6, &m,
                                  &err),
                    -1);
  TEST_CHECK_EQ_INT(xw_mesh_parse(NULL, 0, &m, &err), -1);
  TEST_CHECK_EQ_INT(xw_mesh_parse(NULL, 100, &m, &err), -1);
  xw_mesh_free(NULL);
}

int main(void) {
  TEST_SUITE("scene_mesh");
  test_parse_mesh();
  test_parse_mdlv0021();
  test_rejects();
  TEST_SUMMARY();
}
