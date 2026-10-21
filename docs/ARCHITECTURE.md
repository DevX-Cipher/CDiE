# Architecture

`cdie` is a straight port of the Detect It Easy console scanner. The original
is C++/Qt and splits into `XBinary`/`XPE` (formats), `XScanEngine` (database
and results), `DiE_Script`/`DiE_ScriptEngine` (script driving) and
`QtScript`/`QJSEngine` (JavaScript). This port keeps the same separation but
implements every layer in C99.

The shared implementation lives in xxfclib. `die_engine_init()` installs its
signature scanner as an `xx_scan_engine`, borrowing the loaded database.
Native clients call `xx_scan_file`, `xx_scan_device`, or `xx_scan_memory` and
release returned results through `xx_scan_free_result()` before engine
cleanup. The console, GUI, and DIE library retain the `die_engine_scan_*`
compatibility wrappers, which transfer results into caller-owned
`xx_scan_result` storage released with `xx_scan_result_free()`.

Every layer reaches the runtime only through `core/utils.h` (the `x_` stubs).
On Windows those stubs are implemented on Win32 alone, so the shipped
executable imports nothing but `KERNEL32.dll` — see
[The runtime layer](#the-runtime-layer).

```text
                     ┌──────────────────────────────┐
   command line  ──▶ │ console/main_console.c        │
                     └───────────────┬──────────────┘
                                     │ xx_scan_options
                     ┌───────────────▼──────────────┐
                     │ die_engine/                  │  compatibility entry,
                     │   xx_die_engine_legacy.c     │  caller-owned results
                     └──┬──────────┬────────────┬────┘
                        │          │            │
        ┌───────────────▼──┐  ┌────▼───────┐  ┌─▼──────────────┐
        │ scan/            │  │ die_engine/│  │ scan/          │
        │ xx_scan_db.c     │  │ engine.c   │  │ xx_scan_result.c│
        │ database         │  │ callbacks  │  │ formatting     │
        └──────────────────┘  └────┬───────┘  └────────────────┘
                                   │
                    ┌──────────────▼───────────────┐
                    │ xx_die_engine_scan.c          │
                    │ xx_die_engine_api.c           │
                    │ _init + detect, script API    │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────▼───────────────┐
                    │ js/  JavaScript engine        │
                    │  js_lex → js_parse → js_interp│
                    │  js_builtins, js_regexp       │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────▼───────────────┐
                    │ format/ xb, xpe, xjpeg, xft,  │
                    │         xdisasm               │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────▼───────────────┐
                    │ core/ utils (x_ stubs), cd_*  │
                    │  utils.c   strings, memory,   │
                    │            printf, I/O        │
                    │  utils_fp.c  double <-> text  │
                    │  utils_math.c  libm           │
                    │  utils_entry.c startup, argv  │
                    └──────┬────────────────┬──────┘
                           │                │
              ┌────────────▼─────┐   ┌──────▼─────────────┐
              │ Win32 (KERNEL32) │   │ C standard library │
              │ _WIN32           │   │ everywhere else    │
              └──────────────────┘   └────────────────────┘
```

## Modules

### `core/`

* `utils.[ch]` — **the only contact point with the runtime.** Every standard
  function is reachable through an `x_` prefixed stub: `x_strlen`,
  `x_memcpy`, `x_malloc`, `x_snprintf`, `x_floor`, and so on. `utils.c`,
  `utils_fp.c`, `utils_math.c` and `utils_entry.c` are the only translation
  units that see a platform header; no other file does.

  Three things cannot be functions because they are macros that must expand
  in the caller's frame, so they are exposed as macros instead:

  | Macro | Wraps |
  | --- | --- |
  | `X_VA_LIST`, `X_VA_START`, `X_VA_END`, `X_VA_COPY` | `<stdarg.h>` |
  | `X_OFFSETOF` | `offsetof` |

  `<stdarg.h>` and `<stddef.h>` are compiler headers, not library ones — they
  declare no functions and need nothing linked, so a freestanding build may
  still use them. `<setjmp.h>` deliberately has no wrapper: `setjmp`/`longjmp`
  live in the C runtime, so the parser reports errors with a flag instead of a
  non-local jump.

  Values that are macros in libc become functions: `x_nan()`, `x_inf()`,
  `x_isnan()`. `FILE *` never appears outside `utils.c` — streams are opaque
  `void *` handles from `x_fopen`, `x_stdout()` and `x_stderr()`.

  **Rule for new code: never call a standard function directly.** The single
  exception is `cd_fs.c`, which additionally talks to the platform directory
  APIs (`<windows.h>` / `<dirent.h>`) — those are not standard C and have no
  portable stub.

  The payoff is that retargeting the runtime is a change to a few files rather
  than a sweep over 19 — which is exactly how the Win32-only build below came
  about.

* `utils_fp.c` — `double` ↔ decimal text. See
  [Floating point](#floating-point).
* `utils_math.c` — the `<math.h>` subset the script API needs, computed
  in-house rather than linked.
* `utils_entry.c` — program startup: the linker entry point, `argv`
  construction and the three compiler support routines.
* `cd_common.[ch]` — checked allocation (`cd_malloc` aborts on OOM, and
  routes through `x_malloc`), a growable byte buffer (`CDBuf`), a pointer
  vector (`CDVec`), FNV-1a hashing and ASCII case helpers. Includes
  `utils.h`, which is where `size_t` and `NULL` come from.
* `cd_fs.[ch]` — file reading, directory listing (Win32 `FindFirstFile` and
  POSIX `dirent`), path splitting/joining, executable directory lookup and
  recursive file collection.

### `format/`

* `xb.[ch]` — the binary substrate:
  * `XBFile`: the whole file in memory plus bounds-checked readers
    (`xb_u8` … `xb_f64`, ANSI/UTF-16/UTF-8 string readers).
  * `XBMemoryMap`: a list of regions (`header`, `section`, `overlay`) with
    `offset → address` and `address → offset` translation and `raw size`
    computation (which is what defines the overlay).
  * The **signature engine**: `xb_convert_signature` (DIE syntax → canonical
    hex), `xb_signature_parse` (into `XSigRecord`s) and
    `xb_signature_compare` / `xb_find_signature`.
  * Checksums: entropy, MD5, CRC-32, CRC-16, Adler-32 and the DIE
    "custom CRC-32" used by the import hashes.
* `xpe.[ch]` — the PE parser. Fills one `XPE` struct with the DOS/NT headers,
  data directories, sections, imports (with per-library position hashes),
  export names, the flattened resource tree, the parsed `VS_VERSIONINFO`
  block, the manifest, the debug directory and the Rich header.
  It also parses the **CLI metadata**: the `#Strings`, `#US`, `#Blob` and
  `#GUID` heaps plus the `#~` table stream — heap index widths, coded-index
  widths, per-table row sizes and per-table file offsets — which is what
  `isNetTypePresent`, `isNetMethodPresent`, `isNetFieldPresent`,
  `getNetModuleName` and `getNetAssemblyName` are built on.
* `xjpeg.[ch]` — the JPEG segment walker: JFIF version, `COM` comments, the
  `DQT` hash, `APP*` marker presence, and a small TIFF/IFD reader that
  recovers the EXIF camera make and model.
* `xpng.[ch]` — the PNG chunk walker. Reads `IHDR` (dimensions, bit depth,
  colour type), `pHYs` and `bKGD`, and assembles the description string
  `XPNG::getInfo` produces — `723x464, 8 bits, RGB, pHYs: 3780x3780 meter`.
  Everything else is skipped by chunk length.
* `xapk.[ch]` + `inflate.[ch]` — the APK reader. `xapk` scans the ZIP central
  directory for `AndroidManifest.xml`, and `inflate.c` (a self-contained
  RFC 1951 DEFLATE decoder — no zlib) decompresses it. The manifest is
  Android binary XML, which `xapk` decodes into attribute text so
  `getAndroidManifestRecord("package")` and `("android:versionName")` resolve.
* `xpdf.[ch]` — the PDF reader. It reduces each object to the flat token list
  `XPDF` produces (`handleXpart`), finding objects through classic `xref`
  tables, the deep scan a cross-reference stream needs, and incremental-update
  chains, then answers the key/value queries the database makes: `/Filter`,
  `/Creator`, `/Producer`, `/Author`, the version and the header comment.
  Stream contents are not decompressed — nothing the database asks for lives
  there.
* `xelf.[ch]` — the ELF reader. Header, section table (names resolved through
  `.shstrtab`), program headers, the dynamic library list (`DT_NEEDED` via the
  mapped string table), the `TYPE MACHINE-BITS` general-options string, the
  entry-point file offset (virtual address translated through `PT_LOAD`) and a
  raw-size/overlay estimate. Backs the ELF compiler and library detections.
* `xmach.[ch]` — the Mach-O reader. Walks the load commands for
  `LC_LOAD_DYLIB` libraries (matched by basename), sections (by `sectname`)
  and the `LC_MAIN` entry point.
* `xdex.[ch]` — the Dalvik reader. The version from the magic, the CRC-32 hash
  of the map-item type sequence (the format detail), and the string_ids /
  type_ids tables the obfuscator signatures scan.
* `xft.[ch]` — magic-based file type detection and `xft_check`, the
  equivalent of `XBinary::checkFileType` (so a `PE` database entry matches a
  `PE32`/`PE64` file).
* Disassembly for script rules lives in xxfclib's `die_engine` module. Its
  `xx_die_engine_xdisasm.c` adapter decodes x86 instructions in 16-, 32- and
  64-bit modes and formats Intel text through cdisasm.

### `js/`

See [JS_ENGINE.md](JS_ENGINE.md).

### xxfclib `scan/` and `die_engine/`

* `scan/xx_scan_db.c` — walks the database directory tree, maps each subdirectory to a file
  type, reads every `*.sg` (or extension-less) file and sorts each database by
  `(file type, priority digit, name)`.
* `die_engine/xx_die_engine_engine.c` — installs the native `xx_scan_engine`
  callbacks through `die_engine_init()`. It borrows the database, bounds each
  selected input range, maps native file types to signature types, and owns
  callback results until their engine free callback is called.
* `scan/xx_scan_engine.c` — common detection, callback dispatch, parent-format
  passes, resource, archive and overlay child scans, and result merging for native
  `xx_scan_*` callers. Child scans have distinct scan IDs, retain their parent's
  ID, identify their origin with `scan_part`, and merge offsets into the input.
* `scan/xx_scan_resources.c` — enumerates nonempty PE/.NET resources and ELF
  GResources through the existing format streams, bounded by the selected
  input range. `resources_scan` scans every stored payload once, before the
  overlay pass. Child scans clear all optional traversal flags. Compressed
  GResources are scanned as stored; other file types have no resource pass.
  Resource type names or numeric type IDs reach signature scripts through
  `getScanID()`, and `isResource()` identifies these child scans.
  Malformed ranges, cancellation, allocation failure and more than 65,536
  resource leaves fail the scan and discard partial results.
* `scan/xx_scan_archives.c` — visits ZIP (also JAR/APK/IPA), LHA, GZIP, BZIP2
  and Mach-O Fat children when `archives_scan` is enabled. Compressed output
  uses memory only when its decoded size is less than
  `xx_get_file_buffer_size()`. Equality or a larger size uses `xx_io_temp_open()`;
  temporary storage is cleaned up after scanning and honors an active
  memory-only policy. GZIP/BZIP2 output spills as its size becomes known.
  ZIP Store, ordinary LHA LH0/LZ4 members and Mach-O Fat slices use bounded
  `xx_io_sub` source views after required integrity validation. Masked LHA
  payloads require decoding. Archive child scans use the selected device
  directly, without copying small inputs into memory again. No archive-derived
  output paths are opened. This storage threshold does not bound decoder
  internal buffers. Concatenated GZIP/BZIP2 streams are one logical child.
  Directories are skipped. Encryption, unsupported
  methods, malformed data, checksum failures and limits fail the scan.
  Defaults are 256 MiB per member, 1 GiB total and 100,000 entries; the
  `archive_max_member_size`, `archive_max_total_size` and `archive_max_members`
  options override these when nonzero. Native decoder limits still apply;
  LHA compressed or masked payloads retain their 256 MiB decoded-member ceiling.
  Children disable further traversal.
  Records retain the member name, packed extent and compression state.
  Compressed child offsets refer to decoded data; stored child offsets refer
  to the original input. GUI, text and JSON output identify these separately.
* `die_engine/xx_die_engine_legacy.c` — compatibility wrappers for existing
  callers. Default automatic scans preserve the Binary/COM fallback passes;
  explicit file types run one interpretation. Parent-format, resource, archive or overlay flags
  route through the common scanner. The device wrapper restores the borrowed
  cursor, and memory scanning borrows the input until the synchronous call
  returns. Directory recursion remains the console's responsibility.
* `die_engine/xx_die_engine_api.c` — installs the script API. A single dispatcher function keyed by an
  enum implements ~200 methods on the `Binary`/`PE`/`MSDOS` object plus the
  global helpers (`_setResult`, `_isResultPresent`, `_removeResult`,
  `includeScript`, …). It also carries the two lookup tables behind the
  `--verbose` "Operation system" line: the Windows version names and the
  `IMAGE_FILE_HEADER` machine names.
* `die_engine/xx_die_engine_scan.c` — the signature pass driver, mirroring
  `DiE_Script::processDetect`. It connects progress and cooperative cancellation
  to script boundaries and the `_isStop()` script helper.
* `scan/xx_scan_result.c` — type priorities, the type-name table and the
  text/JSON/XML/CSV/TSV formatters. Each reproduces the corresponding DiE
  output exactly, which for the structured ones means matching quirks of the
  Qt classes that produced them — `QJsonObject` orders keys alphabetically,
  and `QXmlStreamWriter` opens with a blank line.
  `xx_scan_print_text` reuses the same record formatter while writing colored
  spans to the terminal. Category colors come from `scan/xx_scan_color.c`;
  `terminal/xx_terminal_color.c` ports XOptions' nearest-color, ANSI and native
  Windows console mappings. The console flushes buffered headers before direct
  writes and restores attributes/modes before diagnostics or the next file.
  Disabled colors, redirected streams and structured formats remain plain.

## The runtime layer

Because every call site already goes through `utils.h`, the implementation
behind it is free to change. It does, on one axis: `#if defined(_WIN32)`.

**On Windows every stub is Win32.** Not "mostly Win32" — the C standard
library is not called at all, and with the default MSVC 64-bit build it is not
even linked. `x_strlen` is `lstrlenA`, `x_malloc` is `HeapAlloc` on
`GetProcessHeap()`, `x_fopen` is `CreateFileW` and the returned `void *` is a
`HANDLE`, `x_printf` formats into a buffer and hands it to `WriteFile` on
`GetStdHandle(STD_OUTPUT_HANDLE)`. The current Release console image imports 50
`KERNEL32.dll` symbols on x64 and 51 on ARM64; the ARM64 image adds
`IsProcessorFeaturePresent` for CPU capability detection. The import set is:

| Area | Functions |
| --- | --- |
| Memory | `GetProcessHeap`, `HeapAlloc`, `HeapReAlloc`, `HeapFree` |
| Files and metadata | `CreateFileW`, `ReadFile`, `SetFilePointerEx`, `CloseHandle`, `GetFileAttributesW`, `DeleteFileW`, `GetFileInformationByHandle`, `GetFileSizeEx`, `GetFileType`, `LocalFileTimeToFileTime`, `SetFileAttributesW`, `SetFileTime`, `DosDateTimeToFileTime`, `MoveFileExW` |
| Console | `GetStdHandle`, `WriteFile`, `FlushFileBuffers`, `GetConsoleMode`, `SetConsoleMode` |
| Directories and paths | `FindFirstFileW`, `FindNextFileW`, `FindClose`, `GetModuleFileNameA`, `CreateDirectoryW`, `GetFullPathNameW`, `RemoveDirectoryW`, `GetTempPathW` |
| Process, timing, errors and threads | `GetCommandLineW`, `GetEnvironmentVariableA`, `ExitProcess`, `GetTickCount`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `GetLastError`, `SetLastError`, `GetCurrentThreadId` |
| Thread-local storage | `TlsAlloc`, `TlsGetValue`, `TlsSetValue`, `TlsFree` |
| Dynamic loading | `LoadLibraryW`, `GetProcAddress`, `FreeLibrary` |
| Strings | `lstrlenA`, `MultiByteToWideChar`, `WideCharToMultiByte` |
| ARM64 CPU features | `IsProcessorFeaturePresent` |

The file and directory calls are the **wide** forms, and the program is driven
by `GetCommandLineW`, because a `CreateFileA` build cannot even name a file
outside the process ANSI code page — a Cyrillic path arrives as a row of `?`
and the open fails. Paths are UTF-8 everywhere inside the program;
`MultiByteToWideChar` / `WideCharToMultiByte` convert at the Win32 boundary.
The command line is parsed as UTF-16 and each argument converted to UTF-8
before `x_main` sees it.

`lstrcmpA` is conspicuously absent from that list, and on purpose: it is
locale-aware. It would reorder the signature database and change the result of
every JavaScript relational operator on strings. `x_strcmp` is a byte-exact
loop instead.

Everywhere else the same stubs forward to `<string.h>`, `<stdlib.h>`,
`<stdio.h>` and `<math.h>`, exactly as before.

### Entry point

`main` does not exist. `console/main_console.c` defines `x_main`, and what
reaches it depends on the build:

* **CRT-free** (`CDIE_NO_CRT`, the MSVC x64/ARM64 default) — the linker entry is
  `x_entry_point` in `utils_entry.c`. It calls `x_startup`, which splits
  `GetCommandLineW()` into `argv` using the documented Microsoft quoting
  rules, converts each argument to UTF-8, calls `x_main` and passes the result
  to `ExitProcess`.

  The split between the two functions is not cosmetic. On x64 the call into
  `x_startup` establishes the call-relative stack alignment expected by
  compiled code; on ARM64 it preserves the ABI's 16-byte SP alignment.

* **Hosted** — a one-line `main` forwards to `x_main`.

The CRT-free entry object supplies `memset`, `memcpy`, `memmove`, `memcmp`,
`strlen`, `abs`, and `labs`, plus the `_fltused` marker MSVC references from
every object file that touches floating point. Some are emitted by the
compiler for structure initialisation and large copies; the rest cover the few
standard-library calls retained in dependency code, including Debug builds.

They are compiled with `#pragma optimize("", off)`. `#pragma function` stops
the compiler substituting an intrinsic for an explicit call, but it does *not*
stop loop-idiom recognition: at `/O2` a plain byte loop that fills memory is
itself rewritten into `call memset`. Inside the definition of `memset` that is
infinite recursion, and it presents as a stack overflow on the first call.

The freestanding xxfclib build supplies the architecture-specific compiler
helpers that would otherwise come from the runtime. Its x64 and ARM64
`__chkstk` implementations touch each page before a large stack allocation;
the ARM64 helper library also supplies the 32-bit interlocked operations MSVC
emits as out-of-line calls. `x_startup` still heap-allocates `argv`, and the
floating point bignums remain compact, to avoid unnecessary stack pressure.

### Floating point

The database compares version numbers, and the script API exposes
`Number.prototype.toFixed`, so `double` ↔ text has to be correct, not
approximate. Rounding `14.50` to `14.32` is a wrong answer, not a rounding
artefact. `utils_fp.c` therefore carries a small bignum (48 limbs) and uses
it for exact arithmetic:

* `x_strtod` — decimal → `double`. Builds the exact rational the literal
  denotes, normalises it so the denominator sits just below the numerator,
  then extracts 54 bits by long division and rounds to nearest-even with a
  sticky bit. Correctly rounded for every input.
* `x_dtoa_shortest` — `double` → the shortest decimal that reads back
  identically (Steele & White / Dragon4, with the boundary asymmetry at
  powers of two handled). This is what `String(number)` uses.
* `x_dtoa_fixed`, `x_dtoa_precision` — the counted-digit variants behind
  `toFixed` and `toPrecision`, including the case where the value rounds up
  into a place it has no digits for, which is what makes `(0.5).toFixed(0)`
  come out as `"1"`.

`x_snprintf` has **no floating-point conversions at all** — no `%f`, no `%g`.
Nothing in the port needs them, and leaving them out keeps the formatter free
of the bignum. Numbers reach text through the three functions above.

`utils_math.c` supplies the rest. `x_sqrt` uses the hardware wherever the
compiler exposes it without a library call — an SSE2 intrinsic on 64-bit
MSVC, `__builtin_sqrt` on GCC and Clang — and falls back to Newton-Raphson
elsewhere. That fallback ends with one step carried out in double-double
precision, using Dekker's splitting to compute the residual `v - g²` exactly:
plain Newton converges to a fixed point that can sit 1 ULP off, because every
iteration rounds.

`x_log` and `x_exp` use the fdlibm minimax polynomials and the trigonometric
functions are argument reduction on top of the fdlibm kernels. Accuracy there
is within about 1 ULP rather than correctly rounded;
see [LIMITATIONS.md](LIMITATIONS.md#runtime).

## Scan pipeline

1. **Open** the input with `xx_io` and prepare a bounded read-only view of the
   selected range. Small files are buffered; larger files use bounded windows.
2. **Detect** native file types with `xx_scan_detect_type()` and dispatch the
   preferred type through the engine's callbacks. The compatibility default
   path keeps the reference's Binary/COM fallback order. A .NET PE appears as
   `Binary`, `MSDOS`, `PE`, its actual `PE32`/`PE64` width, and `DOTNET`.
   The DIE callback retains that PE width and enables the CLI-assembly flag
   (`XFT_CLI_ASSEMBLY`) for the DOTNET binding below.
3. **Parse** the format with xxfclib's native inspection readers and build the
   memory map. Other inputs get a single flat `Data` region.
4. **Create** a JavaScript context and install the API. The format object is
   bound as `Binary`, the format name (`PE`, `ELF`, …) and — for a .NET PE —
   `DOTNET`.
5. **Run** the global `_init` script (file type "unknown"), then the file-type
   `_init` script, then (for a .NET PE) the `PE/DOTNET/_init`. These define
   `meta()`, `result()`, the `String.prototype` helpers and the `PE.section` /
   `PE.resource` arrays.
6. **Iterate** the signature list in database order. A signature runs when
   * its file type matches (`xft_check`) — or, for a `PE/DOTNET`
     (`XFT_CLI_ASSEMBLY`) script, the file is a .NET PE, and
   * it is not a `DS.*`/`EP.*` script while deep scan is off, and
   * it is not a `HEUR.*` script while heuristic scan is off, and
   * it is not named `_init`, and
   * its database is enabled.

   Each script is evaluated in the global scope and then its `detect()`
   function is called with `(show_type, show_version, show_info)`.
7. **Collect** results through `_setResult`. Duplicates suppressed by
   `_removeResult` are remembered in a blacklist, exactly as
   `DiE_ScriptEngine::_setResultSlot` does.
8. **Sort** the results by type priority with a stable sort, so equal
   priorities keep insertion order.
9. **Format** and print.

## Why the output matches

Three details decide whether the printed lines are identical to `diec`:

* **Signature order.** Each database (main, extra, custom) is sorted
  independently and appended after the previous one — the reference engine
  never re-sorts the combined list. Scripts that check
  `_isResultPresent(...)` before adding a record therefore see the same state.
* **Result order.** `XScanEngine::sortRecords` sorts only by priority; for the
  small lists a single scan produces this behaves like a stable sort, which is
  what `cdie` implements explicitly.
* **String construction.** `createResultStringEx` is reproduced verbatim:
  optional `(Heur)`/`(A-Heur)` prefix, `Type: `, name, `(version)`,
  `[info]`, with the optional space controlled by `--format`.
