#include "common.h"
#include "install.h"
#include "payload.h"
#include "ui.h"
#include <shellapi.h>
#include <wchar.h>

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hprev, LPSTR cmd, int show)
{
	int argc = 0, i, silent = 0, existing = 0, allusers = 0, rc;
	WCHAR** argv;
	InstallOpts o;
	HANDLE mutex;
	WCHAR dir0[MAX_PATH], space[96];
	unsigned long long total;

	(void)hi; (void)hprev; (void)cmd; (void)show;
	if (is_uninstall_mode()) return uninstall_main();

	ZeroMemory(&o, sizeof(o));
	o.desktop = 1; o.context = 1;
	argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (i = 1; argv && i < argc; ++i) {
		if (!lstrcmpiW(argv[i], L"/S")) silent = 1;
		else if (!lstrcmpiW(argv[i], L"/allusers")) o.allusers = 1;
		else if (!lstrcmpiW(argv[i], L"/nodesktop")) o.desktop = 0;
		else if (!lstrcmpiW(argv[i], L"/noctx")) o.context = 0;
		else if (!lstrcmpiW(argv[i], L"/child")) o.child = 1;
		else if (!_wcsnicmp(argv[i], L"/dir=", 5)) lstrcpynW(o.dir, argv[i] + 5, MAX_PATH);
	}
	if (argv) LocalFree(argv);

	if (silent) {
		WCHAR err[256];
		if (!o.dir[0]) default_install_dir(o.allusers, o.dir, MAX_PATH);
		return run_install_flow(&o, NULL, NULL, err, 256) ? 0 : 1;
	}

	mutex = CreateMutexW(NULL, TRUE, L"Local\\cdie-setup-mutex");
	if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

	if (find_existing_install(dir0, MAX_PATH, &allusers)) {
		WCHAR u[MAX_PATH];
		path_join(u, MAX_PATH, dir0, UNINSTALL_EXE);
		existing = (GetFileAttributesW(u) != INVALID_FILE_ATTRIBUTES);   
	}
	else { allusers = 0; default_install_dir(0, dir0, MAX_PATH); }

	total = payload_peek_total();
	if (total) wsprintfW(space, L"About %u MB of disk space required.",
		(unsigned)(total / (1024 * 1024)) + 1);
	else lstrcpyW(space, L"Warning: the installer data is missing.");

	rc = ui_setup_run(dir0, allusers, existing, space);

	if (mutex) CloseHandle(mutex);
	return rc;
}