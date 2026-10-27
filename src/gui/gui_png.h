/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_PNG_H
#define CDIE_GUI_PNG_H

#include <stddef.h>

/* Decodes a non-interlaced 8-bit RGB or RGBA PNG (up to 4096 x 4096) into
 * top-down straight-alpha RGBA8 rows. Returns 1 and an xx_rt_malloc'd buffer
 * (release with xx_rt_free), or 0 for other or malformed images. */
int gui_png_decode_rgba(const unsigned char *data, size_t size, unsigned char **rgba, unsigned int *width, unsigned int *height);

#endif
