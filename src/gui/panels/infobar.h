#ifndef CDIE_INFOBAR_H
#define CDIE_INFOBAR_H
#include "platform/platform.h"

typedef struct {
  const char* label;     /* UTF-8 */
  const char* value;
  const char* subvalue;
} InfoCard;

typedef struct Infobar Infobar;

Infobar* infobar_create(void* parent_native, int x, int y, int w, int h);
void      infobar_destroy(Infobar* d);
PlWidget* infobar_widget(const Infobar* d);
void      infobar_set_cards(Infobar* d, const InfoCard* cards, int count);
void      infobar_clear(Infobar* d);

#endif