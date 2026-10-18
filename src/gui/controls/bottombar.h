#ifndef CDIE_BOTTOMBAR_H
#define CDIE_BOTTOMBAR_H

#include "platform/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

  typedef void (*BottombarCb)(void* user, int button_id);

  typedef struct Bottombar Bottombar;

  Bottombar* bottombar_create(
    void* parent_native,
    int x, int y, int w, int h,
    BottombarCb cb,
    void* user);

  void bottombar_destroy(Bottombar* b);

  PlWidget* bottombar_widget(const Bottombar* b);

  void bottombar_set_status(Bottombar* b, const char* utf8);

#ifdef __cplusplus
}
#endif

#endif /* CDIE_BOTTOMBAR_H */