#include "common.h"
#include <shobjidl.h>
#include <string.h>
#include <wchar.h>

void path_join(WCHAR* out, size_t cap, const WCHAR* a, const WCHAR* b)
{
  size_t n;
  if (!cap) return;
  lstrcpynW(out, a, (int)cap);
  n = (size_t)lstrlenW(out);
  if (n && out[n - 1] != L'\\' && n + 1 < cap) { out[n++] = L'\\'; out[n] = 0; }
  lstrcpynW(out + n, b, (int)(cap - n));
}

int is_elevated(void)
{
  HANDLE tok;
  TOKEN_ELEVATION e;
  DWORD n = 0;
  int r = 0;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
    if (GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n))
      r = e.TokenIsElevated != 0;
    CloseHandle(tok);
  }
  return r;
}

int make_dirs(const WCHAR* path)
{
  int r = SHCreateDirectoryExW(NULL, path, NULL);
  return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

int rel_path_is_safe(const char* p)
{
  const char* s;
  if (!*p || p[0] == '/' || strchr(p, '\\') || strchr(p, ':')) return 0;
  for (s = p; *s; ++s)
    if (s[0] == '.' && s[1] == '.' && (s == p || s[-1] == '/') && (s[2] == '/' || s[2] == 0))
      return 0;
  return 1;
}

void default_install_dir(int allusers, WCHAR* out, size_t cap)
{
  WCHAR base[MAX_PATH], t[MAX_PATH];
  out[0] = 0;
  if (FAILED(SHGetFolderPathW(NULL, allusers ? CSIDL_PROGRAM_FILES : CSIDL_LOCAL_APPDATA,
    NULL, SHGFP_TYPE_CURRENT, base))) return;
  if (allusers) {
    path_join(out, cap, base, APP_NAME);
  }
  else {
    path_join(t, MAX_PATH, base, L"Programs");
    path_join(out, cap, t, APP_NAME);
  }
}


static void reg_sz(HKEY k, const WCHAR* name, const WCHAR* v)
{
  RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v, (DWORD)((lstrlenW(v) + 1) * sizeof(WCHAR)));
}

static void reg_dword(HKEY k, const WCHAR* name, DWORD v)
{
  RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
}

int read_install_location(HKEY root, WCHAR* out, size_t cap)
{
  HKEY k;
  DWORD type = 0, sz = (DWORD)((cap - 1) * sizeof(WCHAR));
  int ok;
  if (RegOpenKeyExW(root, UNINSTALL_KEY, 0, KEY_READ | REGVIEW, &k) != ERROR_SUCCESS) return 0;
  out[0] = 0;
  ok = RegQueryValueExW(k, L"InstallLocation", NULL, &type, (BYTE*)out, &sz) == ERROR_SUCCESS
    && type == REG_SZ;
  RegCloseKey(k);
  if (ok) out[sz / sizeof(WCHAR)] = 0;
  return ok && out[0];
}

int find_existing_install(WCHAR* dir, size_t cap, int* allusers)
{
  if (read_install_location(HKEY_CURRENT_USER, dir, cap)) { *allusers = 0; return 1; }
  if (read_install_location(HKEY_LOCAL_MACHINE, dir, cap)) { *allusers = 1; return 1; }
  return 0;
}

int register_uninstall(int allusers, const WCHAR* dir, DWORD size_kb)
{
  HKEY k;
  HKEY root = allusers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  WCHAR un[MAX_PATH], exe[MAX_PATH], quoted[MAX_PATH + 4], icon[MAX_PATH + 4];

  if (RegCreateKeyExW(root, UNINSTALL_KEY, 0, NULL, 0, KEY_WRITE | REGVIEW, NULL, &k, NULL)
    != ERROR_SUCCESS) return 0;

  path_join(un, MAX_PATH, dir, UNINSTALL_EXE);
  path_join(exe, MAX_PATH, dir, APP_EXE);
  wsprintfW(quoted, L"\"%s\"", un);
  wsprintfW(icon, L"%s,0", exe);

  reg_sz(k, L"DisplayName", APP_TITLE);
  reg_sz(k, L"DisplayVersion", APP_VERSION);
  reg_sz(k, L"Publisher", APP_PUBLISHER);
  reg_sz(k, L"InstallLocation", dir);
  reg_sz(k, L"DisplayIcon", icon);
  reg_sz(k, L"UninstallString", quoted);
  reg_dword(k, L"NoModify", 1);
  reg_dword(k, L"NoRepair", 1);
  reg_dword(k, L"EstimatedSize", size_kb);
  RegCloseKey(k);
  return 1;
}

void unregister_uninstall(int allusers)
{
  RegDeleteKeyExW(allusers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, UNINSTALL_KEY, REGVIEW, 0);
}

int set_context_menu(int allusers, const WCHAR* exe, int enable)
{
  HKEY root = allusers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  HKEY k;
  WCHAR icon[MAX_PATH + 4], cmd[MAX_PATH + 16];

  if (!enable) {
    RegDeleteKeyExW(root, CONTEXT_KEY L"\\command", REGVIEW, 0);
    RegDeleteKeyExW(root, CONTEXT_KEY, REGVIEW, 0);
    return 1;
  }

  wsprintfW(icon, L"\"%s\"", exe);
  wsprintfW(cmd, L"\"%s\" \"%%1\"", exe);

  if (RegCreateKeyExW(root, CONTEXT_KEY, 0, NULL, 0, KEY_WRITE | REGVIEW, NULL, &k, NULL)
    != ERROR_SUCCESS) return 0;
  reg_sz(k, NULL, CONTEXT_LABEL);
  reg_sz(k, L"Icon", icon);
  RegCloseKey(k);

  if (RegCreateKeyExW(root, CONTEXT_KEY L"\\command", 0, NULL, 0, KEY_WRITE | REGVIEW, NULL, &k, NULL)
    != ERROR_SUCCESS) return 0;
  reg_sz(k, NULL, cmd);
  RegCloseKey(k);
  return 1;
}

int create_shortcut(const WCHAR* lnk, const WCHAR* target, const WCHAR* workdir, const WCHAR* desc)
{
  IShellLinkW* psl = NULL;
  IPersistFile* ppf = NULL;
  int ok = 0;

  if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
    &IID_IShellLinkW, (void**)&psl))) return 0;

  psl->lpVtbl->SetPath(psl, target);
  if (workdir) psl->lpVtbl->SetWorkingDirectory(psl, workdir);
  if (desc)    psl->lpVtbl->SetDescription(psl, desc);
  psl->lpVtbl->SetIconLocation(psl, target, 0);

  if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void**)&ppf))) {
    ok = SUCCEEDED(ppf->lpVtbl->Save(ppf, lnk, TRUE));
    ppf->lpVtbl->Release(ppf);
  }
  psl->lpVtbl->Release(psl);
  return ok;
}

void start_menu_dir(int allusers, WCHAR* out, size_t cap)
{
  WCHAR base[MAX_PATH];
  out[0] = 0;
  if (SUCCEEDED(SHGetFolderPathW(NULL, allusers ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS,
    NULL, SHGFP_TYPE_CURRENT, base)))
    path_join(out, cap, base, APP_NAME);
}

void desktop_link(int allusers, WCHAR* out, size_t cap)
{
  WCHAR base[MAX_PATH], name[64];
  out[0] = 0;
  wsprintfW(name, L"%s.lnk", APP_NAME);
  if (SUCCEEDED(SHGetFolderPathW(NULL,
    allusers ? CSIDL_COMMON_DESKTOPDIRECTORY : CSIDL_DESKTOPDIRECTORY,
    NULL, SHGFP_TYPE_CURRENT, base)))
    path_join(out, cap, base, name);
}