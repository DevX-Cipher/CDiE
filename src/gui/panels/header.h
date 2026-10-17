#ifndef CDIE_HEADER_H
#define CDIE_HEADER_H
#include "platform/platform.h"

typedef void (*HeaderBrowseCb)(void* user);
typedef struct Header Header;

Header* header_create(void* parent_native, int x, int y, int w, int h,
  HeaderBrowseCb cb, void* user);
void      header_destroy(Header* hd);
PlWidget* header_widget(const Header* hd);
void      header_set_path(Header* hd, const char* folder_utf8,
  const char* filename_utf8);
void      header_clear(Header* hd);

#endif