#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "scene_mesh.h"

/* Layout taken from linux-wallpaperengine's CImage::loadPuppetMesh: a 9-byte
 * marker, then mesh blocks of [u32 unknown][u32 vertexBytes][vertices]
 * [u32 indexBytes][indices], and an MDLS marker that ends the mesh area. The
 * engine scans for a block instead of trusting a length field, because the
 * bytes right after the marker are not a block. */
#define XW_MESH_MARKER_SIZE 9
#define XW_MESH_HEADER_SIZE 8
#define XW_MESH_STRIDE 80
#define XW_MESH_UV_OFFSET 72

static uint32_t rd32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static float rdfloat(const unsigned char *p) {
  float f;
  memcpy(&f, p, sizeof(f));
  return f;
}

void xw_mesh_free(XWMesh *m) {
  if (!m)
    return;
  free(m->pos);
  free(m->uv);
  free(m->idx);
  memset(m, 0, sizeof(*m));
}

int xw_mesh_parse(const unsigned char *data, size_t len, XWMesh *out,
                  XWError *err) {
  size_t mdls_at, off, vertices_at = 0, indices_at = 0;
  uint32_t vertex_bytes = 0, index_bytes = 0, nv, ni, i;
  int found = 0;
  if (!out)
    return -1;
  memset(out, 0, sizeof(*out));
  if (!data || len < XW_MESH_MARKER_SIZE) {
    XW_ERROR_SET(err, 1, "mesh data too short");
    return -1;
  }
  if (memcmp(data, "MDLV0021", 8) != 0 && memcmp(data, "MDLV0023", 8) != 0) {
    XW_ERROR_SET(err, 1, "unsupported mesh container");
    return -1;
  }
  mdls_at = len;
  for (off = XW_MESH_MARKER_SIZE; off + 4 <= len; off++) {
    if (memcmp(data + off, "MDLS", 4) == 0) {
      mdls_at = off;
      break;
    }
  }
  if (mdls_at == len) {
    XW_ERROR_SET(err, 1, "mesh terminator missing");
    return -1;
  }
  for (off = XW_MESH_MARKER_SIZE;
       off + XW_MESH_HEADER_SIZE + sizeof(uint32_t) < mdls_at; off++) {
    size_t vb_at = off + XW_MESH_HEADER_SIZE;
    size_t ib_at;
    uint32_t vb = rd32(data + off + 4);
    uint32_t ib;
    if (vb == 0 || vb % XW_MESH_STRIDE != 0)
      continue;
    ib_at = vb_at + vb;
    if (ib_at + sizeof(uint32_t) > mdls_at)
      continue;
    ib = rd32(data + ib_at);
    if (ib == 0 || ib % (sizeof(unsigned short) * 3) != 0)
      continue;
    if (ib_at + sizeof(uint32_t) + ib > mdls_at)
      continue;
    found = 1;
    vertices_at = vb_at;
    indices_at = ib_at + sizeof(uint32_t);
    vertex_bytes = vb;
    index_bytes = ib;
    break;
  }
  if (!found) {
    XW_ERROR_SET(err, 1, "no usable mesh block");
    return -1;
  }
  nv = vertex_bytes / XW_MESH_STRIDE;
  ni = index_bytes / (uint32_t)sizeof(unsigned short);
  out->pos = malloc(sizeof(float) * 3u * nv);
  out->uv = malloc(sizeof(float) * 2u * nv);
  out->idx = malloc(sizeof(unsigned short) * ni);
  if (!out->pos || !out->uv || !out->idx) {
    xw_mesh_free(out);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  for (i = 0; i < nv; i++) {
    const unsigned char *v = data + vertices_at + (size_t)i * XW_MESH_STRIDE;
    out->pos[i * 3 + 0] = rdfloat(v + 0);
    out->pos[i * 3 + 1] = rdfloat(v + 4);
    out->pos[i * 3 + 2] = rdfloat(v + 8);
    out->uv[i * 2 + 0] = rdfloat(v + XW_MESH_UV_OFFSET);
    out->uv[i * 2 + 1] = rdfloat(v + XW_MESH_UV_OFFSET + 4);
  }
  for (i = 0; i < ni; i++) {
    out->idx[i] = (unsigned short)((uint32_t)data[indices_at + i * 2] |
                                   ((uint32_t)data[indices_at + i * 2 + 1] << 8));
  }
  out->n_vertices = nv;
  out->n_indices = ni;
  return 0;
}
