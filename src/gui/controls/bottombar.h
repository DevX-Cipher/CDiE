#ifndef CDIE_BOTTOMBAR_H
#define CDIE_BOTTOMBAR_H
#include "platform/platform.h"

enum { BB_SIGNATURES, BB_FLAGS, BB_DATABASE, BB_RUN_SCAN, BB_COUNT };

typedef void (*BottombarCb)(void *user, int button_id);
typedef struct Bottombar Bottombar;

Bottombar *bottombar_create(void *parent_native, int x, int y, int w, int h,
                            BottombarCb cb, void *user);
void       bottombar_destroy(Bottombar *b);
PlWidget  *bottombar_widget(const Bottombar *b);

#endif