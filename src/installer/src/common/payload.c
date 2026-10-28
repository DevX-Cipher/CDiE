#include "payload.h"
#include "resource.h"
#include <string.h>
#include <stdlib.h>
#include "lzma_decoder.h"

#define HEADER_SIZE 20

static const unsigned char* g_data;
static size_t               g_len;
static int                  g_state;
static LZMAByteVector       g_vec;

static unsigned long long rd(const unsigned char* p, int bytes)
{
  unsigned long long v = 0;
  int i;
  for (i = 0; i < bytes; ++i) v |= (unsigned long long)p[i] << (8 * i);
  return v;
}

static const unsigned char* find_res(size_t* size)
{
  HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_PAYLOAD), RT_RCDATA);
  HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
  const unsigned char* p = g ? (const unsigned char*)LockResource(g) : NULL;
  if (p) *size = (size_t)SizeofResource(NULL, r);
  return p;
}

static int load_payload(void)
{
  size_t rsz = 0;
  const unsigned char* res;

  if (g_state) return g_state > 0;
  g_state = -1;

  res = find_res(&rsz);
  if (!res || rsz < 13) return 0;

  if (memcmp(res, "CDPK0001", 8) == 0) {
    g_data = res; g_len = rsz;
  }
  else {
    memset(&g_vec, 0, sizeof(g_vec));
    if (lzma_decompress(res, rsz, &g_vec, ((size_t)-1) / 2) != 0) {
      free(g_vec.data); memset(&g_vec, 0, sizeof(g_vec));
      return 0;
    }
    g_data = g_vec.data; g_len = g_vec.length;
  }
  if (g_len < HEADER_SIZE || memcmp(g_data, "CDPK0001", 8) != 0) return 0;
  g_state = 1;
  return 1;
}

unsigned long long payload_peek_total(void)
{
  size_t rsz = 0;
  const unsigned char* res = find_res(&rsz);
  if (!res || rsz < 20) return 0;
  if (memcmp(res, "CDPK0001", 8) == 0) return rd(res + 12, 8);
  {
    unsigned long long u = rd(res + 5, 8);   
    return u == ~0ULL ? 0 : u;
  }
}

int payload_open(Payload* p)
{
  memset(p, 0, sizeof(*p));
  if (!load_payload()) return 0;

  p->base = g_data;
  p->size = g_len;
  p->count = (unsigned)rd(g_data + 8, 4);
  p->total = rd(g_data + 12, 8);
  p->off = HEADER_SIZE;
  return 1;
}

int payload_next(Payload* p, PayloadEntry* e)
{
  size_t len;
  unsigned long long sz;

  if (p->off == p->size) return 0;
  if (p->size - p->off < 2) return -1;
  len = (size_t)rd(p->base + p->off, 2);
  p->off += 2;
  if (len == 0 || len >= sizeof(e->path) || p->size - p->off < len + 8) return -1;

  memcpy(e->path, p->base + p->off, len);
  e->path[len] = 0;
  p->off += len;

  sz = rd(p->base + p->off, 8);
  p->off += 8;
  if (sz > p->size - p->off) return -1;

  e->size = sz;
  e->data = p->base + p->off;
  p->off += (size_t)sz;
  return 1;
}