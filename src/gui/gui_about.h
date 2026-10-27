/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_ABOUT_H
#define CDIE_GUI_ABOUT_H

#include <stddef.h>
#include <xxwidgets/xxwidgets.h>

/* Detect It Easy's About for every frontend: Follow me buttons, the Info and
 * Libraries lines, build date, Thanks and Check updates links, and the plain
 * text fields that text-only layouts show. png is the picture (optional; the
 * terminal shows none). Destroy with xxwidgets_about_dialog_destroy. */
xxwidgets_status gui_about_create(const unsigned char *png, size_t png_size, xxwidgets_about_dialog **about);

/* The About picture embedded at build time (cdie_gui only). */
extern const unsigned char cdie_about_png[];
extern const size_t cdie_about_png_size;

#endif
