#ifndef XW_WE_COLOR_H
#define XW_WE_COLOR_H

/* Album-art palette extraction. Pure C: no GLib/GTK/WebKit so it can be
 * unit-tested without a display or bus. Pixels are packed RGB/RGBA. */
int xw_we_color_palette(const unsigned char *px, int w, int h, int rowstride,
                        int n_channels, char primary[8], char secondary[8],
                        char tertiary[8], char text[8], char high[8]);

#endif /* XW_WE_COLOR_H */
