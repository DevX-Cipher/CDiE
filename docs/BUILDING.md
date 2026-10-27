# Building and testing

## Requirements

A C11 compiler and CMake 3.16+. The CRT-free MSVC ARM64 build needs CMake
3.26+ for `ASM_MARMASM`. No Qt or code generation is required. The
xxfclib engine uses `cdisasm` for x86 instruction decoding and Intel text
formatting. CMake builds `cdisasm` from source with xxfclib and links it
statically, so it adds no DLL dependency. In the local source layout,
`cdisasm` lives at `_mylibs/cdisasm`; initialize the bundled dependencies in a
Git checkout before building. Set `-DXXFC_CDISASM_DIR=<path>` if the sources
are elsewhere. Its license and generated-data notices are in the `cdisasm`
source tree.

The earlier scanner build was tested with MSVC 19.44 (Visual Studio 2022) on
Windows and GCC 11.4 on Linux, with identical scan output. The `cdisasm`
version builds with Clang 21 on Linux and passes the two available CDiE tests.
Recheck corpus parity after the decoder migration. The console and Cocoa GUI
also build with AppleClang on macOS ARM64; the GUI smoke test exercises a
native Mach-O scan. This does not replace a full corpus parity run.

The CDiE sources compile as **strict ISO C99** — `CMAKE_C_EXTENSIONS` is `OFF`,
so GCC gets `-std=c99` rather than `-std=gnu99` for those sources. The xxfclib
and cdisasm dependencies use C11. Strict C99 hides POSIX declarations behind
feature-test macros, which is why `src/app/cdie_app.c` defines
`_POSIX_C_SOURCE` before its first include. A new CDiE source file that calls
POSIX functions needs the same preamble.

## Build

For a Git checkout, initialize the bundled dependencies first:

```bash
git submodule update --init --recursive
```

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build
```

The console binary lands in `cdie_build/src/console/cdie` (`.exe` on Windows).
The terminal frontend is `cdie_build/src/tui/cdie_tui`; macOS and Windows also
build `cdie_gui`. Select frontends with `CDIE_BUILD_CONSOLE`, `CDIE_BUILD_TUI`,
and `CDIE_BUILD_GUI`. Console and TUI default to ON; the native GUI defaults
to ON only on macOS and Windows. GUI/TUI require CMake 3.21+ and `xxwidgets`
(set `CDIE_XXWIDGETS_DIR` to override its location).

For a console-only build, set `CDIE_BUILD_TUI=OFF` and `CDIE_BUILD_GUI=OFF`.
Set `CDIE_BUILD_LIBRARY=OFF` as well to omit the separate library targets.
TUI builds with `CDIE_BUILD_GUI=OFF` use the terminal backend without a native
desktop toolkit.

### Terminal interface

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release \
  -DCDIE_DATABASE_DIR=/path/to/Detect-It-Easy
cmake --build cdie_build --target cdie cdie_tui cdie_gui --parallel
cdie_build/src/tui/cdie_tui /path/to/target
```

On Linux, enable the GTK GUI with `-DCDIE_BUILD_GUI=ON` and install the GTK 3
development package (`gtk+-3.0` >= 3.16). Without it, omit `cdie_gui` from the
build command. The TUI accepts an optional startup file and shares the scanner's
engine, result tree, flag and database controls, and full report display.
Use `cdie_tui --help` for terminal requirements and keyboard shortcuts.
Run it in an interactive terminal rather than redirecting input or output;
`--help` and `--version` also work with redirected output. On Windows, append
`.exe` to the executable name.

Enter or F5 scans the path, Tab switches controls, Ctrl+O focuses the path,
Ctrl+R opens the complete report, F1 displays help, and Ctrl+Q quits. The shared
scan panel provides flags, optional databases, a colored result tree and a
cancellable progress dialog. Scans run on a worker thread. `-D`, `-E` and `-C`
select signature directories; otherwise the executable-directory/working-directory
search applies. The TUI uses a hosted C runtime, as the desktop GUI does.

### Linux GTK GUI

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release -DCDIE_BUILD_GUI=ON
cmake --build cdie_build --target cdie_gui --parallel
cdie_build/src/gui/cdie_gui /path/to/target
```

For a headless startup and scan check, run `xvfb-run -a
cdie_build/src/gui/cdie_gui --smoke-test /path/to/target`. Signature databases
selected by `CDIE_DATABASE_DIR` are staged beside the executable. The Linux
frontend also opens without a database; use Database paths (F3) to load the main,
extra and custom folders. Fonts (F4) edits the shared role-based font settings.
Both settings apply for the current session.

With `CDIE_BUILD_TESTS=ON` and `xvfb-run` installed, `cdie.linux_settings`
exercises the actual GTK dialogs, failed-load rollback and retry, folder chooser
acceptance/cancellation, default paths, font validation and rendered font changes.
It repeats the checks without a database beside the executable to cover recovery.

The build stages `db/`, `db_extra/`, and `db_custom/` from `CDIE_DATABASE_DIR`
beside the console and TUI executables. The macOS GUI carries the same folders
inside its app bundle. `CDIE_DATABASE_DIR=NONE` disables this staging.

With the local thin wrapper, configure and build all three using:

```bash
cmake -S ~/ownCloud_build/cdie -B ~/ownCloud_build/cdie/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCDIE_BUILD_GUI=ON -DCDIE_BUILD_TUI=ON -DCDIE_BUILD_CONSOLE=ON
cmake --build ~/ownCloud_build/cdie/build --target cdie_gui cdie_tui cdie --parallel
```

The wrapper puts these outputs under `build/cdie/src/gui/`, `build/cdie/src/tui/`,
and `build/cdie/src/console/`, respectively.

### macOS GUI

Use Xcode Command Line Tools (including the macOS SDK and Objective-C compiler)
and CMake 3.21+ for the xxwidgets Cocoa backend. The GUI is built automatically:

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release \
  -DCDIE_DATABASE_DIR=/path/to/Detect-It-Easy
cmake --build cdie_build --parallel
open cdie_build/src/gui/cdie_gui.app
```

`CDIE_DATABASE_DIR` contains `db/`, optionally `db_extra/` and `db_custom/`.
The app copies these into `Contents/Resources` when linked, so it can be moved
and opened from Finder. Without an explicit path it uses the bundled dependency
or the local sibling database, like the console package. Set the option to
`NONE` to omit databases and select external folders using **Database paths**.
Successful folder selections are saved in the app's macOS preferences.

The GUI provides file picking, file drops, Finder/Dock file opening, a parser
selector, the result tree, full text reports, scan options and optimization
settings. Cmd+O opens a file; Cmd+R, F5 or Return scans it. The Windows Explorer
integration is specific to the Windows front end.

Run the Cocoa smoke test in a logged-in desktop session:

```bash
cdie_build/src/gui/cdie_gui.app/Contents/MacOS/cdie_gui --smoke-test \
  cdie_build/src/console/cdie
```

It checks bundled database loading, scanning, explicit Binary selection, report
contents and missing-file recovery. With `CDIE_BUILD_TESTS=ON` it is also
registered as the `gui_cocoa` CTest test when a database is available.

### Windows, MSVC + Ninja

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S cdie_source -B cdie_build
ninja -C cdie_build
```

### The CRT-free Windows build

On native x64 and ARM64 MSVC the executable links **no C runtime at all**:
the option `CDIE_NO_CRT` defaults to `ON`, the entry point becomes
`x_entry_point` instead of `mainCRTStartup`, and the only import is
`KERNEL32.dll`.

```text
> dumpbin /imports cdie.exe
    KERNEL32.dll
```

The link flags that produce this live in `src/console/CMakeLists.txt`:
`/NODEFAULTLIB /ENTRY:x_entry_point /SUBSYSTEM:CONSOLE`, plus `/GS-` to drop
the stack cookie (`__security_check_cookie` is a CRT symbol).

Turn it off if you need to link something that expects a runtime:

```bash
cmake -S cdie_source -B cdie_build -DCDIE_NO_CRT=OFF
```

It is already off for Win32 and ARM64EC MSVC, for non-MSVC compilers and for
every non-Windows target — see [LIMITATIONS.md](LIMITATIONS.md#runtime) for
why.

Only the console `cdie` executable is CRT-free. The libraries (below) link the
dynamic CRT so they interoperate with normally-compiled consumers.

Two rules apply when adding code that will be compiled into this build:

* **No standard library calls**, including implicit ones. Go through the `x_`
  stubs in `core/utils.h`.
* The freestanding xxfclib build supplies the compiler helpers needed by each
  supported architecture: a page-by-page `__chkstk` probe on x64 and ARM64,
  plus the ARM64 interlocked operations MSVC emits out of line. Large frames
  therefore retain normal Windows guard-page growth without importing the CRT.

### Windows GUI

The Windows build also produces `src/gui/cdie_gui.exe`. Its native front end
uses `dep/xxwidgets` when populated, otherwise the sibling
`../_mylibs/xxwidgets` (override `CDIE_XXWIDGETS_DIR` when it is elsewhere).
The reusable `xxwidgets_scan_panel` provides a result tree, Flags
and Databases checkbox combo boxes, and Scan and Report buttons. The detected
format is the root, with colored `Type: Name (Version) [Info]` detection rows.
Overlay nodes contain their format, offset, size and nested detections. This
tree layout is shared with macOS and the GTK/TUI widget backends. Report opens
the full formatted engine output and supports copying it.

Flags and optional database selections take effect on the next scan; the main
database is always used. Options edits the same values and keeps both combo
boxes synchronized. The Database paths button edits signature folders;
failed reloads and Cancel retain the previously loaded database. About uses
the reusable xxwidgets dialog. Enter or F5 scans, Ctrl+O opens a file, and
Ctrl+Shift+D opens database paths.

The GUI uses a hosted C runtime. With `CDIE_NO_CRT=ON`, the GUI and xxwidgets
link the runtime statically, while the console retains its CRT-free entry
point and import behavior.

### The libraries

The build also produces a shared and a static library exposing the
die_library C API — `die.dll`/`die.lib` and `die_static.lib` on Windows,
`libdie.so`/`libdie.a` on Linux, and `libdie.dylib`/`libdie.a` on macOS — from
the same engine sources (minus the console's `core/utils_entry.c`). They are
on by default:

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build           # builds cdie, die_shared, die_static
cmake -S cdie_source -B cdie_build -DCDIE_BUILD_LIBRARY=OFF   # frontends only
```

See [LIBRARY.md](LIBRARY.md) for the API, the flag mapping and how to compile a
consumer (use `/MD` on MSVC to match the libraries' dynamic CRT).

### Install

```bash
cmake --install cdie_build --prefix /where/you/want
```

On Linux the binary goes to `bin/`; elsewhere to the prefix root. The install
also places `README.md`, `LICENSE`, `changelog.txt` and `docs/` next to the
binary, plus cdisasm's license and generated-data notices under
`licenses/cdisasm/` and the signature database when one was found (see below).

## Packaging

### GitHub Beta packages

Run **Build and publish Beta** from the GitHub Actions tab on `master`. The
workflow runs only when started manually. It builds portable ZIP packages on
Ubuntu 24.04, Windows 2022, and macOS 15 on Apple Silicon (arm64), then
publishes all three to the `Beta` prerelease after checking that `master` still
points at the packaged commit.

The builds use the same resolved revisions of `xxfclib`, `cdisasm`, and the
Detect It Easy signature database; Windows also uses `xxwidgets`. These are
checked out from the `horsicq` GitHub repositories into `dep/`. If any of those
repositories are private, add a `CDIE_DEPS_TOKEN` Actions secret with read
access to them. Public repositories work with the workflow's default token.

### Windows portable package

```bat
packaging\windows\build_portable_windows.cmd <cmake-platform> <package-suffix>
```

```bat
packaging\windows\build_portable_windows.cmd x64   win64_msvc2022
packaging\windows\build_portable_windows.cmd Win32 win32_msvc2022
packaging\windows\build_portable_windows.cmd ARM64 winarm64_msvc2022
```

Unlike the Qt projects in this repository the script takes **no Qt root** —
`cdie` builds xxfclib and its cdisasm decoder from source, so the platform and
the package suffix are the only arguments.

The default generator is Ninja. It builds the focused `die_engine` archive
and cdisasm decoder faster than a Visual Studio generator. Run the script from
any prompt:
when `cl.exe` is not already on `PATH` it finds Visual Studio with `vswhere`
and calls `vcvarsall` for the requested platform (`x64`, `x64_x86` for `Win32`,
`x64_arm64` for `ARM64`). From a Developer Command Prompt it uses that
environment, but stops if the prompt targets a different architecture.

Following the repo convention, build trees and CPack staging live under
`%TEMP%` and only finished artefacts land in `release\`:

```text
release\
├── cdie_win64_msvc2022_portable_1.0.0\   ready-to-run folder
│   ├── cdie.exe
│   ├── db\  db_extra\  db_custom\
│   ├── docs\
│   ├── licenses\cdisasm\
│   └── README.md  LICENSE  changelog.txt
└── cdie_win_x64_portable_1.0.0.zip       CPack ZIP
```

Environment overrides:

| Variable | Effect |
| --- | --- |
| `CMAKE_GENERATOR_NAME` | generator, default `Ninja`; a Visual Studio generator (e.g. `Visual Studio 17 2022`) also works but builds much more slowly |
| `CDIE_DATABASE_DIR` | directory holding `db`, `db_extra`, `db_custom`; `NONE` packages the binary alone |

### Bundling the signature database

A scanner without signatures is not useful, so the install rules bundle a
database when they can find one. The CMake cache variable
`CDIE_DATABASE_DIR` controls this:

* unset — use `dep/Detect-It-Easy` if it has a `db` subdirectory, otherwise
  use the sibling `../_mylibs/Detect-It-Easy` if available;
* a path — use that directory's `db`, `db_extra` and `db_custom`;
* `NONE` — package the executable only.

```bash
cmake -S cdie_source -B cdie_build -DCDIE_DATABASE_DIR=/opt/detect-it-easy
cmake -S cdie_source -B cdie_build -DCDIE_DATABASE_DIR=NONE
```

Because `cdie` looks for `db`, `db_extra` and `db_custom` next to its own
executable, an extracted package runs with no arguments at all:

```text
> cdie.exe C:\utils\python3\python.exe
PE64
    Linker: Microsoft Linker(14.50.35225)
    Compiler: Microsoft Visual C/C++(19.50.35225)[POGO_O_C]
    ...
```

### Other platforms

There is no `.deb`/`.pkg` script yet. `cpack -G TGZ` (or `DEB`) against a
configured build tree works, since the CPack metadata is set up in the root
`CMakeLists.txt`:

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build
cpack --config cdie_build/CPackConfig.cmake -G TGZ
```

## Running

```bash
cdie -D /path/to/Detect-It-Easy/db \
     -E /path/to/Detect-It-Easy/db_extra \
     -C /path/to/Detect-It-Easy/db_custom \
     target.exe
```

Without `-D`/`-E`/`-C` the tool looks for `db`, `db_extra` and `db_custom`
next to the executable, then in the working directory.

## Tests

```bash
cmake -S cdie_source -B cdie_build -DCDIE_BUILD_TESTS=ON
cmake --build cdie_build
cdie_build/tests/cdie_test_runtime
```

`tests/test_runtime.c` covers the runtime layer — the formatter, both
directions of the `double` ↔ text conversions, the math subset and the string
and memory stubs. It exits with the number of failures, so `ctest` or a shell
script can use it directly. See
[TESTING.md](TESTING.md#testing-the-runtime-primitives).

## Comparing against the reference

The point of the port is byte-identical output, so the natural test is a diff
against `diec`:

```powershell
$root = "C:\path\to\Detect-It-Easy"
$a = & "$root\diec.exe" $target | Out-String
$b = & cdie.exe -D "$root\db" -E "$root\db_extra" -C "$root\db_custom" $target | Out-String
if ($a.Trim() -eq $b.Trim()) { "IDENTICAL" } else { "DIFFERENT" }
```

```bash
diff <(diec "$target") <(cdie -D "$root/db" -E "$root/db_extra" -C "$root/db_custom" "$target")
```

### Two traps when comparing on Windows

* **Pass the same databases.** `diec` loads `db`, `db_extra` and `db_custom`
  from its own directory. If you only give `cdie` the main database, extra
  detections such as `Microsoft Visual C/C++(…, by EP)` will be missing.
* **Watch WOW64 redirection.** The stock `diec.exe` is a 32-bit binary, so
  `C:\Windows\System32\foo.dll` silently resolves to `SysWOW64\foo.dll` for
  it but not for a 64-bit `cdie`. Compare inside `SysWOW64` (or any
  non-redirected directory) to avoid diffing two different files.

## Checking the database parses

A quick way to validate the JavaScript front end against a whole database is
to parse every file and report syntax errors. The engine exposes
`js_parse_program()` for exactly this; a ~40 line harness that walks a
directory is enough. Against the stock database every one of the 2098
signature scripts parses; the only failures are the `.png`, `.txt` and
`.json` files that live in sub-directories the loader ignores anyway.

## Layout of the build

`src/CMakeLists.txt` collects every source file into `CDIE_SOURCES` and
`src/console/CMakeLists.txt` links them into the executable. Adding a new
format parser means dropping the files into `src/format/` and adding them to
`CDIE_SOURCES`; adding script API functions means one enum value, one
`switch` case and one table row in `src/engine/api.c`.
