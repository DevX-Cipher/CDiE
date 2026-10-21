#pragma once
#include "theme/theme.h"   /* PlWidget */

typedef struct ResultsTree ResultsTree;

ResultsTree *resultstree_create(void *parent_native, int x, int y, int w, int h);
void         resultstree_destroy(ResultsTree *t);
PlWidget    *resultstree_widget(const ResultsTree *t);

void resultstree_clear(ResultsTree *t);
/* Strings are copied. Returns 0 on out-of-memory. */
int  resultstree_add(ResultsTree *t, const char *type, const char *name,
                     const char *version, const char *info);
int  resultstree_count(const ResultsTree *t);