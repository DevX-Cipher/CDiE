#ifndef CDIE_SIDEBAR_H
#define CDIE_SIDEBAR_H
#include "platform/platform.h"

enum { NAV_OVERVIEW, NAV_SECTIONS, NAV_SIGNATURES, NAV_STRINGS, NAV_HEX,
       NAV_DISASM, NAV_ENTROPY, NAV_VIRUSTOTAL, NAV_YARA, NAV_OPTIONS, NAV_COUNT };

typedef void (*SidebarCb)(void *user, int nav_id);
typedef struct Sidebar Sidebar;

Sidebar  *sidebar_create(void *parent_native, int x, int y, int w, int h,
                         SidebarCb cb, void *user);
void      sidebar_destroy(Sidebar *s);
PlWidget *sidebar_widget(const Sidebar *s);
void      sidebar_set_selected(Sidebar *s, int nav_id);
int       sidebar_get_selected(const Sidebar *s);

#endif
