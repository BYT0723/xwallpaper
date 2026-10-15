#ifndef XW_SCENE_MESH_H
#define XW_SCENE_MESH_H

#include <stddef.h>

#include "xw_err.h"

/* One MDLV puppet mesh, laid out the way the engine reads it
 * (linux-wallpaperengine CImage::loadPuppetMesh): 80-byte vertices with the
 * position at offset 0 and the UV at offset 72, then uint16 indices that come
 * in triangles. The mesh is static; the engine applies no skeletal animation
 * to the positions either. */
typedef struct XWMesh {
  float *pos; /* 3 floats per vertex */
  float *uv;  /* 2 floats per vertex */
  unsigned int n_vertices;
  unsigned short *idx;
  unsigned int n_indices;
} XWMesh;

/* Parse an MDLV container. MDLV0021 and MDLV0023 are the versions the engine
 * reads; MDLV0016 is rejected here exactly as it is there, because its layout
 * is not known. Anything malformed fails closed with -1 and leaves `out`
 * zeroed. */
int xw_mesh_parse(const unsigned char *data, size_t len, XWMesh *out,
                  XWError *err);

void xw_mesh_free(XWMesh *m);

#endif /* XW_SCENE_MESH_H */
