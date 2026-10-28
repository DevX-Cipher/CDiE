#ifndef COMMON_H
#define COMMON_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <shlobj.h>
#include <stddef.h>

#define APP_NAME       L"cdie"
#define APP_TITLE      L"Detect It Easy - cdie"
#define APP_VERSION    L"1.0.0"
#define APP_PUBLISHER  L"hors"
#define APP_EXE        L"cdie_gui.exe"
#define DB_SUBDIR      L"db"          
#define CONTEXT_LABEL  L"Scan with cdie"


#define UNINSTALL_EXE  L"uninstall.exe"
#define LIST_FILE      L"uninstall.lst"
#define UNINSTALL_KEY  L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\cdie"
#define CONTEXT_KEY    L"Software\\Classes\\*\\shell\\cdie"
#define REGVIEW        KEY_WOW64_64KEY

typedef struct {
  WCHAR dir[MAX_PATH];
  int allusers;   /* install for all users (Program Files, HKLM) */
  int desktop;    /* create desktop shortcut */
  int context;    /* add Explorer context menu entry */
  int child;      /* running as the elevated helper of another installer */
} InstallOpts;

typedef void (*ProgressCb)(void* ud, int percent, const WCHAR* text, int step);

void path_join(WCHAR* out, size_t cap, const WCHAR* a, const WCHAR* b);
int  is_elevated(void);
int  make_dirs(const WCHAR* path);
int  rel_path_is_safe(const char* utf8_rel);

void default_install_dir(int allusers, WCHAR* out, size_t cap);
int  read_install_location(HKEY root, WCHAR* out, size_t cap);
int  find_existing_install(WCHAR* dir, size_t cap, int* allusers);

int  create_shortcut(const WCHAR* lnk, const WCHAR* target, const WCHAR* workdir, const WCHAR* desc);
void start_menu_dir(int allusers, WCHAR* out, size_t cap);
void desktop_link(int allusers, WCHAR* out, size_t cap);

int  register_uninstall(int allusers, const WCHAR* dir, DWORD size_kb);
void unregister_uninstall(int allusers);
int  set_context_menu(int allusers, const WCHAR* exe, int enable);

#endif
