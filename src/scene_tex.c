#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef XW_HAVE_LZ4
#include <lz4.h>
#endif

#include "scene_tex.h"

static uint32_t rd32be(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int xw_tex_find_image(const unsigned char *data, size_t len, size_t *img_off,
                      size_t *img_len, unsigned int *w, unsigned int *h) {
  if (!data || len < 12) return -1;
  if (!img_off || !img_len || !w || !h) return -1;
  if (memcmp(data, "TEXV", 4) != 0) return -1;
  static const unsigned char magic[8] = {0x89, 'P', 'N', 'G',
                                         0x0D, 0x0A, 0x1A, 0x0A};
  size_t off = 4;
  int found = 0;
  for (; off + 8 <= len; off++) {
    if (memcmp(data + off, magic, 8) == 0) {
      found = 1;
      break;
    }
  }
  if (!found) return -1;
  if (off + 33 > len) return -1;
  if (memcmp(data + off + 12, "IHDR", 4) != 0) return -1;
  *img_off = off;
  *img_len = len - off;
  *w = rd32be(data + off + 16);
  *h = rd32be(data + off + 20);
  if (*w == 0 || *h == 0 || *w > 16384 || *h > 16384) return -1;
  return 0;
}


/* ---- raw TEXV decode ----
 * Layouts follow linux-wallpaperengine's TextureParser: TEXV0005 header,
 * TEXI0001 dimensions, TEXB0001-4 containers, LZ4 mipmaps. Level-0 mipmap
 * only; animations/video rejected. */

static uint32_t rd32le(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

typedef struct XWTexCursor {
  const unsigned char *base;
  size_t len;
  size_t pos;
} XWTexCursor;

static int cur_bytes(XWTexCursor *c, void *out, size_t n, XWError *err,
                     const char *what) {
  if (!c || c->pos + n > c->len || c->pos + n < c->pos) {
    XW_ERROR_SET(err, 1, "tex truncated at %s", what);
    return -1;
  }
  memcpy(out, c->base + c->pos, n);
  c->pos += n;
  return 0;
}

static int cur_u32(XWTexCursor *c, uint32_t *v, XWError *err, const char *what) {
  unsigned char b[4];
  if (cur_bytes(c, b, 4, err, what) != 0)
    return -1;
  *v = rd32le(b);
  return 0;
}

static int cur_magic(XWTexCursor *c, const char *want, XWError *err) {
  char got[9];
  memset(got, 0, sizeof(got));
  if (cur_bytes(c, got, 9, err, "magic") != 0)
    return -1;
  if (memcmp(got, want, 8) != 0) {
    XW_ERROR_SET(err, 1, "bad tex magic");
    return -1;
  }
  return 0;
}

void xw_tex_image_free(XWTexImage *img) {
  if (!img)
    return;
  free(img->px);
  memset(img, 0, sizeof(*img));
}

/* WE texture format ids (see reference Texture.h) */
#define XW_TEX_ARGB8888 0u
#define XW_TEX_DXT5 4u
#define XW_TEX_DXT3 6u
#define XW_TEX_DXT1 7u
#define XW_TEX_RG88 8u
#define XW_TEX_R8 9u
/* FreeImage ids; PNG and MP4 are the embedded files we act on */
#define XW_FIF_PNG 13u
#define XW_FIF_MP4 35u /* aliases FIF_WEBP, as in the engine */
#define XW_FIF_UNKNOWN 0xFFFFFFFFu
/* WE TextureFlags_Video: the single mip is an MP4 rather than pixels */
#define XW_TEX_FLAG_VIDEO 32u

static size_t dxt_bytes(uint32_t w, uint32_t h, int block) {
  size_t bw = ((size_t)w + 3) / 4, bh = ((size_t)h + 3) / 4;
  return bw * bh * (size_t)block;
}

/* Sprite-sheet animation block (TEXS0001/0002/0003) follows the images; the
 * engine uses it to pick a frame per tick. We only read the first mipmap,
 * so scan for the magic instead of walking every mipmap. */
static void parse_texs(const unsigned char *data, size_t len, XWTexImage *img) {
  static const char *m3 = "TEXS0003", *m2 = "TEXS0002", *m1 = "TEXS0001";
  size_t off;
  uint32_t count, i;
  int v1 = 0;
  img->n_frames = 0;
  img->frame_time = 0.0f;
  if (!data || len < 16)
    return;
  for (off = 0; off + 16 <= len; off++) {
    if (memcmp(data + off, m3, 8) == 0 || memcmp(data + off, m2, 8) == 0 ||
        memcmp(data + off, m1, 8) == 0)
      break;
  }
  if (off + 16 > len)
    return;
  v1 = memcmp(data + off, m1, 8) == 0;
  count = rd32le(data + off + 9);
  off += 13;
  if (memcmp(data + off - 13, m3, 8) == 0) {
    /* TEXS0003 carries the gif (frame) size after the count */
    off += 8;
  }
  if (count == 0 || count > 4096)
    return;
  for (i = 0; i < count && img->n_frames < XW_TEX_FRAMES_MAX; i++) {
    XWTexFrame *f = &img->frames[img->n_frames];
    if (off + 32 > len)
      break;
    f->time = 0.0f;
    if (v1) {
      /* u32 number, float time, u32 x, y, w1, ?, ?, h1 */
      f->time = *(const float *)(data + off + 4);
      f->x = (float)rd32le(data + off + 8);
      f->y = (float)rd32le(data + off + 12);
      f->w = (float)rd32le(data + off + 16);
      f->h = (float)rd32le(data + off + 28);
    } else {
      /* u32 number, float time, float x, y, w1, w2, h2, h1 */
      f->time = *(const float *)(data + off + 4);
      f->x = *(const float *)(data + off + 8);
      f->y = *(const float *)(data + off + 12);
      f->w = *(const float *)(data + off + 16);
      f->h = *(const float *)(data + off + 28);
    }
    img->frame_time += f->time;
    img->n_frames++;
    off += 32;
  }
}

int xw_tex_decode(const unsigned char *data, size_t len, XWTexImage *img,
                  XWError *err) {
  XWTexCursor c = {data, len, 0};
  uint32_t format, flags, texw, texh, w, h, fif = XW_FIF_UNKNOWN;
  uint32_t images, mipmaps, mw, mh, comp;
  uint32_t is_video = 0;
  int32_t usize = 0, csize = 0;
  char container[9];
  size_t expect = 0;
  memset(img, 0, sizeof(*img));
  memset(container, 0, sizeof(container));
  if (!data || len < 64) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (cur_magic(&c, "TEXV0005", err) != 0)
    return -1;
  if (cur_magic(&c, "TEXI0001", err) != 0)
    return -1;
  if (cur_u32(&c, &format, err, "format") != 0 ||
      cur_u32(&c, &flags, err, "flags") != 0 ||
      cur_u32(&c, &texw, err, "texw") != 0 ||
      cur_u32(&c, &texh, err, "texh") != 0 ||
      cur_u32(&c, &w, err, "w") != 0 || cur_u32(&c, &h, err, "h") != 0)
    return -1;
  {
    uint32_t ign;
    if (cur_u32(&c, &ign, err, "reserved") != 0)
      return -1;
  }
  img->flags = flags;
  (void)texw;
  (void)texh;
  if (cur_bytes(&c, container, 9, err, "container") != 0)
    return -1;
  if (memcmp(container, "TEXB0004", 8) == 0) {
    /* 0004 adds an isVideo flag after the file id; non-video entries
     * parse exactly like 0003 (reference downgrades the version). */
    if (cur_u32(&c, &images, err, "images") != 0)
      return -1;
    if (images < 1 || images > 64) {
      XW_ERROR_SET(err, 1, "bad image count");
      return -1;
    }
    if (cur_u32(&c, &fif, err, "fif") != 0)
      return -1;
    if (fif != XW_FIF_UNKNOWN && fif != XW_FIF_PNG && fif != XW_FIF_MP4) {
      XW_ERROR_SET(err, 1, "unsupported embedded format %u", fif);
      return -1;
    }
    if (cur_u32(&c, &is_video, err, "isvideo") != 0)
      return -1;
  } else if (memcmp(container, "TEXB0003", 8) == 0) {
    /* NOTE: image count comes first here; the reference reads the
     * embedded-file id first, which only fits newer files */
    if (cur_u32(&c, &images, err, "images") != 0)
      return -1;
    if (images < 1 || images > 64) {
      XW_ERROR_SET(err, 1, "bad image count");
      return -1;
    }
    if (cur_u32(&c, &fif, err, "fif") != 0)
      return -1;
    if (fif != XW_FIF_UNKNOWN && fif != XW_FIF_PNG && fif != XW_FIF_MP4) {
      XW_ERROR_SET(err, 1, "unsupported embedded format %u", fif);
      return -1;
    }
  } else if (memcmp(container, "TEXB0001", 8) != 0 &&
             memcmp(container, "TEXB0002", 8) != 0) {
    XW_ERROR_SET(err, 1, "unsupported container");
    return -1;
  } else {
    if (cur_u32(&c, &images, err, "images") != 0)
      return -1;
    if (images < 1 || images > 64) {
      XW_ERROR_SET(err, 1, "bad image count");
      return -1;
    }
  }
  if (cur_u32(&c, &mipmaps, err, "mipmaps") != 0)
    return -1;
  if (mipmaps < 1 || mipmaps > 32) {
    XW_ERROR_SET(err, 1, "bad mipmap count");
    return -1;
  }
  if (cur_u32(&c, &mw, err, "mw") != 0 || cur_u32(&c, &mh, err, "mh") != 0 ||
      cur_u32(&c, &comp, err, "comp") != 0)
    return -1;
  {
    unsigned char b[8];
    if (cur_bytes(&c, b, 8, err, "sizes") != 0)
      return -1;
    usize = (int32_t)rd32le(b);
    csize = (int32_t)rd32le(b + 4);
  }
  if (mw == 0 || mh == 0 || mw > 8192 || mh > 8192) {
    XW_ERROR_SET(err, 1, "bad mip size");
    return -1;
  }
  if (comp != 0 && comp != 1) {
    XW_ERROR_SET(err, 1, "unsupported compression %u", comp);
    return -1;
  }
  if (csize <= 0 || usize < 0 || (size_t)csize > c.len - c.pos) {
    XW_ERROR_SET(err, 1, "bad mip sizes");
    return -1;
  }
  if ((is_video == 1) || fif == XW_FIF_MP4 || (flags & XW_TEX_FLAG_VIDEO)) {
    /* A video texture's single mip is a whole MP4, so the pixel-size checks
     * below do not apply; hand the bytes up for the player instead. The
     * shipped 3610097897 texture carries fif=UNKNOWN, isVideo=0 and
     * TextureFlags_Video, which is why the flag is checked too. */
    if ((size_t)csize > c.len - c.pos) {
      XW_ERROR_SET(err, 1, "bad video payload size");
      return -1;
    }
    img->px = malloc((size_t)csize);
    if (!img->px) {
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    memcpy(img->px, c.base + c.pos, (size_t)csize);
    img->len = (size_t)csize;
    img->is_video = 1;
    img->fmt = XW_TEXFMT_MP4;
    img->w = w ? w : mw;
    img->h = h ? h : mh;
    img->tex_w = mw;
    img->tex_h = mh;
    return 0;
  }
  if (fif == XW_FIF_PNG) {
    /* embedded file: hand the raw bytes up for imlib */
    img->px = malloc((size_t)csize);
    if (!img->px) {
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    memcpy(img->px, c.base + c.pos, (size_t)csize);
    img->len = (size_t)csize;
    img->fmt = XW_TEXFMT_PNG;
    img->w = w ? w : mw;
    img->h = h ? h : mh;
    img->tex_w = mw;
    img->tex_h = mh;
    /* embedded-file textures can carry a TEXS sheet too */
    parse_texs(data, len, img);
    return 0;
  }
  switch (format) {
  case XW_TEX_ARGB8888:
    img->fmt = XW_TEXFMT_RGBA8;
    expect = (size_t)mw * mh * 4;
    break;
  case XW_TEX_R8:
    img->fmt = XW_TEXFMT_R8;
    expect = (size_t)mw * mh;
    break;
  case XW_TEX_RG88:
    img->fmt = XW_TEXFMT_RG88;
    expect = (size_t)mw * mh * 2;
    break;
  case XW_TEX_DXT1:
    img->fmt = XW_TEXFMT_DXT1;
    expect = dxt_bytes(mw, mh, 8);
    break;
  case XW_TEX_DXT3:
    img->fmt = XW_TEXFMT_DXT3;
    expect = dxt_bytes(mw, mh, 16);
    break;
  case XW_TEX_DXT5:
    img->fmt = XW_TEXFMT_DXT5;
    expect = dxt_bytes(mw, mh, 16);
    break;
  default:
    XW_ERROR_SET(err, 1, "unsupported tex format %u", format);
    return -1;
  }
  if ((size_t)usize != expect) {
    XW_ERROR_SET(err, 1, "size mismatch");
    return -1;
  }
  img->px = malloc(expect ? expect : 1);
  if (!img->px) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  if (comp == 1) {
#ifdef XW_HAVE_LZ4
    int got = LZ4_decompress_safe((const char *)c.base + c.pos, (char *)img->px,
                                  csize, usize);
    if (got < 0 || (size_t)got != (size_t)usize) {
      free(img->px);
      img->px = NULL;
      XW_ERROR_SET(err, 1, "lz4 decode failed");
      return -1;
    }
#else
    free(img->px);
    img->px = NULL;
    XW_ERROR_SET(err, 1, "lz4 support not compiled in");
    return -1;
#endif
  } else {
    if ((size_t)csize != expect) {
      free(img->px);
      img->px = NULL;
      XW_ERROR_SET(err, 1, "size mismatch");
      return -1;
    }
    memcpy(img->px, c.base + c.pos, expect);
  }
  img->len = expect;
  img->w = w ? w : mw;
  img->h = h ? h : mh;
  img->tex_w = mw;
  img->tex_h = mh;
  parse_texs(data, len, img);
  return 0;
}

int xw_tex_resolve(const char *dir, const char *material, const char *ref,
                   char *out, size_t n) {
  static const char *exts[] = {"", ".tex", ".png", ".jpg"};
  char base[1024], anchor[1024];
  size_t i;
  if (!dir || !ref || !ref[0] || !out || n == 0)
    return -1;
  if (strstr(ref, "..") != NULL)
    return -1;
  if (strchr(ref, '/') != NULL) {
    snprintf(anchor, sizeof(anchor), "%s", dir);
  } else {
    const char *slash = material ? strrchr(material, '/') : NULL;
    if (slash && (size_t)(slash - material) < sizeof(anchor) - 1) {
      size_t dl = (size_t)(slash - material);
      memcpy(anchor, material, dl);
      anchor[dl] = '\0';
      snprintf(base, sizeof(base), "%.512s/%.400s", dir, anchor);
      snprintf(anchor, sizeof(anchor), "%.1000s", base);
    } else {
      snprintf(anchor, sizeof(anchor), "%s", dir);
    }
  }
  for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
    struct stat st;
    snprintf(base, sizeof(base), "%.512s/%.400s%s", anchor, ref, exts[i]);
    if (stat(base, &st) == 0 && S_ISREG(st.st_mode)) {
      snprintf(out, n, "%.1000s", base);
      return 0;
    }
  }
  return -1;
}
