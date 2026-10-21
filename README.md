# [C]DiE — Detect It Easy console scanner, pure C port

`cdie` is a C99 re-implementation of the **Detect It Easy**
console scanner (`diec`). It loads the stock DIE signature database, runs the
JavaScript detection rules and prints the same result strings as the original
Qt-based tool.

Everything needed to do that lives in this source tree:

* a **JavaScript (ES5 subset) interpreter** — lexer, parser, tree-walking
  evaluator, standard library and a backtracking regular-expression engine;
* **binary format parsers** — PE/PE+ (sections, imports, exports, resources,
  version info, manifest, debug directory, Rich header and the full .NET
  metadata tables), ELF, Mach-O, DEX, JPEG/EXIF, PNG, APK (ZIP + a
  from-scratch DEFLATE + Android binary XML) and PDF, plain-text detection,
  plus file-type detection;
* the **DIE signature engine** — the `'text'`, `..`, `**`, `%%`, `$$`, `##`,
  `++` signature syntax, memory maps and RVA/VA translation;
* the **script API** (`PE.*`, `Binary.*`, `_setResult`, `includeScript`, …).

No Qt or code generators. The scan engine uses the C11 `cdisasm` x86 decoder
through xxfclib. Both libraries build from source with the scanner, and the
decoder adds no runtime library dependency.

On Windows it goes one step further: the runtime layer is Win32 only, and the
native MSVC x64 and ARM64 builds link no C runtime at all. The current Release
console executable imports **50 functions on x64 and 51 on ARM64, all from
`KERNEL32.dll`**; ARM64 adds `IsProcessorFeaturePresent` for its CPU feature
switches. That includes its own
correctly-rounded `strtod`/`dtoa` and its own libm. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#the-runtime-layer).

Before the decoder migration, the scanner matched `diec` on a 348-file corpus
(Windows system DLLs, Visual Studio .NET assemblies, Qt binaries, Python,
scripts, text, images, an APK and a PDF): **348 identical, 0 different**. A
30-file subset also matched byte for byte across `--json`, `--xml`, `--csv`,
`--tsv`, `--plaintext` and `--verbose`. These parity results need to be
rechecked with the `cdisasm` backend. See [docs/TESTING.md](docs/TESTING.md).

---

## Quick start

For a Git checkout, initialize the bundled dependencies first:

```bash
git submodule update --init --recursive
```

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build
```

A portable Windows package contains the executable, database, docs, and
decoder license notices:

```bat
packaging\windows\build_portable_windows.cmd x64 win64_msvc2022
```

```bash
cdie -D /path/to/Detect-It-Easy/db /path/to/target.exe
```

Example, matching `diec` byte for byte:

```text
PE64
    Linker: Microsoft Linker(14.50.35225)
    Compiler: Microsoft Visual C/C++(19.50.35225)[LTCG/C]
    Tool: Microsoft Visual Studio(2026, 18.0-18.3)
    Sign tool: Windows Authenticode(2.0)[PKCS #7]
    Debug data: Records[CodeView, VC Feature, POGO]
```

## Terminal interface

The `cdie_tui` target builds an interactive terminal scanner using `xxwidgets`
and the same scan engine, options and colored result tree as the native GUI:

```bash
cmake --build cdie_build --target cdie_tui --parallel
cdie_build/src/tui/cdie_tui /path/to/target
```

Use the file-path field and Scan control to scan another file, or Report to
view the complete output. Enter or F5 scans the path; Tab moves between controls.
Ctrl+O focuses the path field, Ctrl+R opens the full report, F1 displays keyboard
help, and Ctrl+Q exits. Scans run on a worker thread with progress and cancellation.

Run it in an interactive terminal; `--help` shows its options and shortcuts.
`-D`, `-E`, and `-C` select the main, extra, and custom signature directories.
Signature databases selected by `CDIE_DATABASE_DIR` are copied beside the console
and TUI executables during linking. Without explicit paths, databases are resolved
beside the executable or in the working directory.

`CDIE_BUILD_CONSOLE`, `CDIE_BUILD_TUI`, and `CDIE_BUILD_GUI` select frontends.
Console and TUI default to enabled; the GUI defaults to enabled on macOS and
Windows. See [docs/BUILDING.md](docs/BUILDING.md#terminal-interface) for details.

## macOS GUI

The macOS build also produces `src/gui/cdie_gui.app`. Open the app in Finder,
choose a file with **Open** (Cmd+O), or drop a file onto its window or Dock icon.
Window drops use the shared xxwidgets file-drop event on macOS and Windows,
including over the result tree and path field. A drop starts scanning the first
file; if several files are dropped together, the remaining files are ignored.
Press **Scan** (Cmd+R or F5), select another file type to rescan, or open
**Report** for the complete engine output. The native Cocoa front end reuses
the xxwidgets scan panel, result tree, scan options and optimization dialogs.

The result tree starts with the detected format and shows one compact
`Type: Name (Version) [Info]` row per detection. Heuristic markers and category
colors are preserved. Overlay scans appear as expandable children with their
format, hexadecimal offset and size, followed by their own detections. Windows
uses the same tree model; xxwidgets also renders it in GTK and terminal UIs.
Category colors come from the C port of XScanEngine in
`xxfclib/src/scan/xx_scan_color.c`, with its light/dark defaults, protection
aliases and malware background colors.

The app bundles the signature databases selected by `CDIE_DATABASE_DIR` and
finds them independently of the working directory. **Database paths** can load
other signature folders; successful choices are remembered for later runs.
See [docs/BUILDING.md](docs/BUILDING.md#macos-gui) for build and smoke-test commands.

## Windows GUI

`cdie_gui.exe` uses the reusable xxwidgets scan panel and options dialogs.
Choose or drop a file, then select a file type to scan it again with that
parser. **Scan options** edits the flags and optional databases; **Fonts**
chooses fonts for controls, tables, result trees and text. OK applies these
choices for the current run; Cancel preserves the previous values.

**Optimization** sets the general memory buffer and file I/O buffer sizes
through value comboboxes, including 1 KiB, 2 KiB, 4 KiB and 8 KiB. It also
controls SSE2, SSE4.2, AVX2, AES-NI, SHA-NI, VPCLMULQDQ, Arm NEON, Arm CRC32,
Arm AES/SHA crypto and Arm Dot Product acceleration preferences.
SHA-NI applies to SHA-1 and SHA-256; VPCLMULQDQ applies to CRC64 when AVX2
is also enabled. On Arm builds the six x86 controls are hidden, leaving only
the four Arm controls. On x86 and x64 builds the four Arm controls are hidden,
leaving only the six x86 controls. A visible switch is disabled when the build,
CPU or OS does not support it. The four Arm switches expose hardware-gated runtime preferences;
scan algorithms do not consume them yet. **Defaults** restores both buffers to
64 KiB and enables every acceleration option supported by the current CPU and
operating system. OK updates xxfclib's runtime settings for subsequent scans;
Cancel keeps the settings that were active before the dialog opened.

**Context menu** enables or removes the current-user Explorer entry
**Scan with cdie**, following XOptions' quoted executable and file-argument
registration. Explorer settings remain registered until disabled. A file
argument also loads and scans directly:

```bat
cdie_gui.exe "C:\path with spaces\sample.exe"
```

---

## Command line

```text
cdie [options] target
```

The common xxfclib terminal options enable or disable acceleration preferences
before scanning. X86 builds expose SSE2, SSE4.2, AVX2, AES-NI, SHA-NI, and
VPCLMULQDQ. Arm builds expose NEON, CRC32, AES/SHA crypto, and dot-product.
Options for the other architecture are hidden and rejected. Supported features
are on by default; switches for a CPU or OS feature unavailable on the current
machine are accepted and ignored. The last switch for each feature wins.

Disable every x86 preference with:

```bash
cdie --nosse2 --nosse42 --noavx2 --noaes --nosha --novpclmulqdq -D /path/to/Detect-It-Easy/db /path/to/target.exe
```

Disable every Arm preference with:

```bash
cdie --noneon --noarmcrc32 --noarmcrypto --noarmdotprod --noarmpmull -D /path/to/Detect-It-Easy/db /path/to/target.exe
```

Hyphenated `--no-*` spellings are accepted. Arm features may be enabled with
`--arm-crc32`, `--arm-crypto`, `--arm-dotprod`, and `--arm-pmull`; SSE4.2 also accepts
`--sse4.2`, `--nosse4.2`, and `--no-sse4.2`.

Use `--` before a target whose name starts with a dash. Normal terminal output
colors each detection using XScanEngine's category rules and console palette.
`--nocolor` disables colors; `--color` enables them again. Redirected output,
`--plaintext`, JSON, XML, CSV and TSV remain plain. ANSI terminals and native
Windows consoles are supported, and colors are restored after every row.

The short options are the ones `diec` uses, so existing command lines work
unchanged. Note that several are not the mnemonic you would guess — `-a` is
`--alltypes`, not aggressive scan, and `-p` is `--plaintext`, not profiling.
Where `cdie` previously used a different letter, that spelling is still
accepted as an alias (shown in parentheses) except where it would collide.

| Option | Meaning |
| --- | --- |
| `-h`, `-?`, `--help` | show usage |
| `-v`, `--version` | show the version |
| `-r`, `--recursivescan` | descend into directories |
| `-d`, `--deepscan` | enable deep-scan signatures (`DS.*`, `EP.*`) |
| `-u` (`-he`), `--heuristicscan` | enable heuristic signatures (`HEUR.*`) |
| `-g`, `--aggressivecscan` | aggressive scan (`--aggressivescan` also accepted) |
| `-b` (`-V`), `--verbose` | verbose detections (operation system, language, …) |
| `-a` (`-A`), `--alltypes` | accepted, not implemented (see LIMITATIONS.md) |
| `-f`, `--format` | put a space before `(version)` and `[info]` |
| `-U` (`-hu`), `--hideunknown` | drop `Unknown: Unknown` results |
| `-M` (`-m`), `--messages` | print engine/script messages on stderr |
| `-l`, `--profiling` | enable script profiling flags |
| `-j`, `--json` | JSON output |
| `-x`, `--xml` | XML output |
| `-c`, `--csv` / `-t`, `--tsv` | delimited output |
| `-p` (`-P`), `--plaintext` | plain text output |
| `-D`, `--database <path>` | main database directory |
| `-E`, `--extradatabase <path>` | extra database directory |
| `-C`, `--customdatabase <path>` | custom database directory |
| `-s`, `--showdatabase` | print database paths and signature counts |

Output formats are mutually exclusive and may be selected only once. JSON and
XML remain single documents when several files are scanned; the multi-file
form records each path with its scan result. They also cover version/help,
database reports and database TAR creation without mixing plain text into
standard output.

`diec`'s `-S`/`--struct`, `-w`/`--showstructs`, `-e`/`--entropy`,
`-i`/`--info`, `--test` and `--createtest` are not ported; see
[docs/LIMITATIONS.md](docs/LIMITATIONS.md).

When a database path is not given, `cdie` looks for `db`, `db_extra` and
`db_custom` next to the executable and then in the working directory.

Exit codes follow the original tool: `0` success, `1` file not found,
`2` cannot open file, `3` database not found, `4` invalid parameter. This port
also returns `5` when writing or flushing standard output fails.

---

## Library

The build also produces a **shared and a static library** exposing the same C
API as [horsicq/die_library](https://github.com/horsicq/die_library) — the same
`die.h`, the same flags, the same exported functions — so a program written
against die_library (its samples included) builds against this one unchanged.

```text
die_shared -> die.dll  + die.lib      (Windows)   /  libdie.so  (Unix)
die_static -> die_static.lib          (Windows)   /  libdie.a   (Unix)
public header: lib/die.h
```

```c
#include "die.h"
char *r = DIE_ScanFileA("target.exe", DIE_DEEPSCAN | DIE_HEURISTICSCAN, "db");
printf("%s", r);
DIE_FreeMemoryA(r);
```

Exported: `DIE_ScanFile{A,W}`, `DIE_ScanMemory{A,W}`, `DIE_LoadDatabase{A,W}`,
`DIE_ScanFileEx{A,W}`, `DIE_ScanMemoryEx{A,W}`, `DIE_FreeMemory{A,W}` and (on
Windows) `DIE_VB_ScanFile[Callback]`. A worked sample is in
[samples/C](samples/C); see [docs/LIBRARY.md](docs/LIBRARY.md) and
[docs/BUILDING.md](docs/BUILDING.md). Turn it off with `-DCDIE_BUILD_LIBRARY=OFF`.

---

## Documentation

| Document | Contents |
| --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | module map and the scan pipeline |
| [docs/JS_ENGINE.md](docs/JS_ENGINE.md) | the JavaScript engine: grammar, objects, memory model, regexps |
| [docs/SCRIPT_API.md](docs/SCRIPT_API.md) | every function the signature scripts can call |
| [docs/DATABASE.md](docs/DATABASE.md) | database layout, load order, priorities, signature syntax |
| [docs/BUILDING.md](docs/BUILDING.md) | building and installing |
| [docs/LIBRARY.md](docs/LIBRARY.md) | the die_library-compatible shared/static library API |
| [docs/TESTING.md](docs/TESTING.md) | comparing against `diec`, tracing, robustness notes |
| [docs/LIMITATIONS.md](docs/LIMITATIONS.md) | what is deliberately simplified |

---

## Layout

```text
cdie_source/
├── CMakeLists.txt
├── release_version.txt
├── README.md
├── LICENSE
├── changelog.txt
├── docs/
├── packaging/
│   └── windows/
│       └── build_portable_windows.cmd
├── run/                      convenience launchers (cmd + sh)
├── tests/                    runtime tests (-DCDIE_BUILD_TESTS=ON)
├── lib/                      die_library-compatible library (die.h + die.c)
├── samples/                  die_library C sample, builds against lib/
├── release/                  built packages (git-ignored)
└── src/
    ├── CMakeLists.txt
    ├── global.h              application constants
    ├── core/                 utils*.c (the only runtime contact point:
    │                         strings, memory, printf, dtoa, libm, startup),
    │                         buffers, vectors, filesystem
    ├── js/                   the JavaScript engine
    ├── format/               binary access, signatures, PE, file types, disasm
    ├── engine/               database, script API, scan driver, output
    └── console/              command line front end
```

## License

MIT — see [LICENSE](LICENSE).

ARM64 builds also expose `--armpmull` / `--noarmpmull` (aliases
`--arm-pmull` / `--no-arm-pmull`) and **Use Arm PMULL** in Optimization.
PMULL is enabled when detected. The accelerated CRC32/CRC32C paths require
both Arm CRC32 and PMULL enabled, and apply to buffers of at least 3072 bytes.
Disabling either feature retains the existing fallback paths.
