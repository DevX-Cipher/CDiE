#ifndef CDIE_TABBAR_H
#define CDIE_TABBAR_H
#include "platform/platform.h"

enum { TAB_SCAN, TAB_MEMORY, TAB_DISASM, TAB_HEX, TAB_STRINGS,
       TAB_HASH, TAB_ENTROPY, TAB_MORE, TAB_COUNT };

typedef void (*TabbarCb)(void *user, int tab_id);
typedef struct Tabbar Tabbar;

Tabbar   *tabbar_create(void *parent_native, int x, int y, int w, int h,
                        TabbarCb cb, void *user);
void      tabbar_destroy(Tabbar *t);
PlWidget *tabbar_widget(const Tabbar *t);
void      tabbar_set_active(Tabbar *t, int tab_id);
int       tabbar_get_active(const Tabbar *t);

#endif