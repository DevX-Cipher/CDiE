#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <windows.h>

typedef struct {
  const unsigned char* base;
  size_t size, off;
  unsigned count;
  unsigned long long total;
} Payload;

typedef struct {
  char path[512];                
  unsigned long long size;
  const unsigned char* data;      
} PayloadEntry;

int payload_open(Payload* p);
int payload_next(Payload* p, PayloadEntry* e);   
unsigned long long payload_peek_total(void);

#endif