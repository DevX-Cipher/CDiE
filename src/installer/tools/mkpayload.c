#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#define FSEEK _fseeki64
#define FTELL _ftelli64
#else
#include <dirent.h>
#include <sys/stat.h>
#define FSEEK fseeko
#define FTELL ftello
#endif

#define R_GetTickCount GetTickCount
#include "lzma_encoder.h"
#include "lzma_decoder.h"

typedef struct { char rel[512]; char full[2048]; uint64_t size; } Ent;

static Ent*   g_ents;
static size_t g_n, g_cap;
static uint64_t g_total;

static FILE* open_rb(const char* path)
{
#ifdef _WIN32
  wchar_t w[2048];
  if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 2048)) return NULL;
  return _wfopen(w, L"rb");
#else
  return fopen(path, "rb");
#endif
}

static int add_file(const char* full, const char* rel)
{
  FILE* f;
  Ent* e;
  long long sz;

  if (strlen(rel) >= sizeof(e->rel) || strlen(full) >= sizeof(e->full)) {
    fprintf(stderr, "path too long: %s\n", full);
    return 0;
  }
  f = open_rb(full);
  if (!f) { fprintf(stderr, "cannot open: %s\n", full); return 0; }
  FSEEK(f, 0, SEEK_END);
  sz = (long long)FTELL(f);
  fclose(f);
  if (sz < 0) { fprintf(stderr, "cannot size: %s\n", full); return 0; }

  if (g_n == g_cap) {
    size_t nc = g_cap ? g_cap * 2 : 64;
    Ent* n = (Ent*)realloc(g_ents, nc * sizeof(*n));
    if (!n) { fprintf(stderr, "out of memory\n"); return 0; }
    g_ents = n; g_cap = nc;
  }
  e = &g_ents[g_n++];
  strcpy(e->rel, rel);
  strcpy(e->full, full);
  e->size = (uint64_t)sz;
  g_total += e->size;
  return 1;
}

#ifdef _WIN32
static void walk(const char* dir, const char* rel)
{
  char pat[2048];
  wchar_t wpat[2048];
  WIN32_FIND_DATAW fd;
  HANDLE h;

  snprintf(pat, sizeof(pat), "%s\\*", dir);
  if (!MultiByteToWideChar(CP_UTF8, 0, pat, -1, wpat, 2048)) return;
  h = FindFirstFileW(wpat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    char name[1024], full[2048], nrel[512];
    WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), NULL, NULL);
    if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
    snprintf(full, sizeof(full), "%s\\%s", dir, name);
    snprintf(nrel, sizeof(nrel), "%s%s%s", rel, rel[0] ? "/" : "", name);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk(full, nrel);
    else add_file(full, nrel);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}
#else

static void walk(const char* dir, const char* rel)
{
  DIR* d = opendir(dir);
  struct dirent* de;
  if (!d) return;
  while ((de = readdir(d)) != NULL) {
    char full[2048], nrel[512];
    struct stat st;
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
    snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
    snprintf(nrel, sizeof(nrel), "%s%s%s", rel, rel[0] ? "/" : "", de->d_name);
    if (stat(full, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) walk(full, nrel);
    else if (S_ISREG(st.st_mode)) add_file(full, nrel);
  }
  closedir(d);
}
#endif

static int cmp_ent(const void* a, const void* b)
{
  return strcmp(((const Ent*)a)->rel, ((const Ent*)b)->rel);
}

static void put_le(FILE* f, uint64_t v, int bytes)
{
  int i;
  for (i = 0; i < bytes; ++i) fputc((int)((v >> (8 * i)) & 0xFF), f);
}

static int write_payload(const char* out)
{
  FILE* f = fopen(out, "wb");
  static unsigned char buf[1 << 20];
  size_t i;

  if (!f) { fprintf(stderr, "cannot create %s\n", out); return 0; }
  fwrite("CDPK0001", 1, 8, f);
  put_le(f, (uint64_t)g_n, 4);
  put_le(f, g_total, 8);

  for (i = 0; i < g_n; ++i) {
    Ent* e = &g_ents[i];
    FILE* in = open_rb(e->full);
    uint64_t left = e->size;
    if (!in) { fprintf(stderr, "cannot open: %s\n", e->full); fclose(f); return 0; }
    put_le(f, (uint64_t)strlen(e->rel), 2);
    fwrite(e->rel, 1, strlen(e->rel), f);
    put_le(f, e->size, 8);
    while (left) {
      size_t want = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
      size_t got = fread(buf, 1, want, in);
      if (got != want) { fprintf(stderr, "read error: %s\n", e->full); fclose(in); fclose(f); return 0; }
      fwrite(buf, 1, got, f);
      left -= got;
    }
    fclose(in);
  }
  return fclose(f) == 0;
}

static void progress(size_t done, size_t total, void* ud)
{
  (void)ud;
  printf("\rcompressing... %3d%%", total ? (int)(done * 100 / total) : 100);
  fflush(stdout);
}

static int compress_file(const char* path)
{
  FILE* f = open_rb(path);
  long long n;
  uint8_t* raw, * packed;
  size_t csz = 0;
  LZMAByteVector chk;
  FILE* o;

  if (!f) { fprintf(stderr, "cannot reopen %s\n", path); return 0; }
  FSEEK(f, 0, SEEK_END);
  n = (long long)FTELL(f);
  FSEEK(f, 0, SEEK_SET);
  raw = (uint8_t*)malloc(n > 0 ? (size_t)n : 1);
  if (!raw || fread(raw, 1, (size_t)n, f) != (size_t)n) {
    fprintf(stderr, "read failed: %s\n", path);
    fclose(f); free(raw); return 0;
  }
  fclose(f);

  packed = lzma_compress(raw, (size_t)n, &csz, NULL, progress, NULL);
  printf("\n");
  if (!packed) { fprintf(stderr, "compression failed\n"); free(raw); return 0; }

  memset(&chk, 0, sizeof(chk));
  if (lzma_decompress(packed, csz, &chk, (size_t)n) != 0 ||
    chk.length != (size_t)n || memcmp(chk.data, raw, (size_t)n) != 0) {
    fprintf(stderr, "verify FAILED: compressed data does not round-trip\n");
    free(raw); free(packed); free(chk.data); return 0;
  }
  free(chk.data);

  o = fopen(path, "wb");
  if (!o || fwrite(packed, 1, csz, o) != csz || fclose(o) != 0) {
    fprintf(stderr, "cannot write %s\n", path);
    free(raw); free(packed); return 0;
  }
  printf("compressed %llu -> %llu bytes (%.1f%% of original, saved %.1f%%), verified\n",
    (unsigned long long)n, (unsigned long long)csz,
    n ? 100.0 * (double)csz / (double)n : 0.0,
    n ? 100.0 - 100.0 * (double)csz / (double)n : 0.0);
  free(raw); free(packed);
  return 1;
}

#ifdef _WIN32
static char** utf8_args(int* argc)
{
  int i, n;
  wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &n);
  char** v;
  if (!w) return NULL;
  v = (char**)calloc((size_t)n + 1, sizeof(char*));
  for (i = 0; i < n; ++i) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
    v[i] = (char*)malloc((size_t)len);
    WideCharToMultiByte(CP_UTF8, 0, w[i], -1, v[i], len, NULL, NULL);
  }
  *argc = n;
  LocalFree(w);
  return v;
}
#endif

int main(int argc, char** argv)
{
  int i, have_exe = 0;

#ifdef _WIN32
  { int n = 0; char** v = utf8_args(&n); if (v) { argc = n; argv = v; } }
#endif

  if (argc < 3) {
    fprintf(stderr, "usage: mkpayload out.bin <folder> [name=file ...]\n");
    return 2;
  }
  walk(argv[2], "");
  for (i = 3; i < argc; ++i) {
    char* eq = strchr(argv[i], '=');
    if (!eq) { fprintf(stderr, "bad argument: %s\n", argv[i]); return 2; }
    *eq = 0;
    if (!add_file(eq + 1, argv[i])) return 1;
  }

  qsort(g_ents, g_n, sizeof(Ent), cmp_ent);
  for (i = 0; i < (int)g_n; ++i)
    if (!strcmp(g_ents[i].rel, "cdie.exe")) have_exe = 1;
  if (!have_exe)
    fprintf(stderr, "warning: no cdie.exe found in %s (the installer will refuse to run)\n", argv[2]);

  if (!write_payload(argv[1])) return 1;
  printf("packed %u files, %llu bytes (%.2f MB) -> %s\n",
    (unsigned)g_n, (unsigned long long)g_total, g_total / 1048576.0, argv[1]);
  if (!compress_file(argv[1])) return 1;
  return 0;
}