# Testing

The acceptance criterion for this port is simple: for the same input file and
the same databases, `cdie` must print exactly what `diec` prints.

## Reference comparison

### Public compatibility tests

The public suite requires Python 3.8+ and generates its own original PE,
resource, overlay, PNG, ZIP, GZIP and BZIP2 fixtures plus a minimal signature
database. No private samples or installed signature database are needed.

```bash
cmake -S . -B build -DCDIE_BUILD_TESTS=ON -DCDIE_BUILD_LIBRARY=OFF
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

`cdie.compatibility` exercises console defaults, `-a` and clustered aliases,
all four expansion modes, combinations, stored versus decoded archive offsets,
one-level expansion, directory recursion, malformed ZIP CRCs, failures and
JSON/XML/CSV/TSV/plaintext output. It also compiles the real GUI backend
without a desktop toolkit and checks mode changes, separate format groups,
parent-before-child tree ordering and cancellation.

File Information coverage uses independently generated 32/64-bit PE and
little-/big-endian ELF and Mach-O files. It verifies native header values,
structure/field offsets, repeated table entries, inventory counts, catalog
scope, field selectors, argument errors, malformed native tables, output
escaping and multi-file documents. These tests deliberately point at a missing
signature database to prove that native information commands do not load it.
Native DataStructs output has a separate documented schema; the `diec`
differential suite below continues to cover signature-scan output.

When xxwidgets is built, `cdie_test_file_information` compares the GUI model
with console DataStructs for all generated executable fixtures, including
absolute field offsets. Shared-view tests destroy the source model before
navigating the copied widget data, verify failed replacement preserves the
old view, and check typed ENTRY fields. A POSIX PTY test opens the actual shared
dialog, navigates and closes it with Escape. Both pre-cancelled and mid-read
observer cancellation return no partial model. Windows' native smoke test
opens File Information and checks for populated field-table rows; Cocoa's
smoke test exercises the shared form and native model in its actual window.

To compare with a reference executable, configure
`-DCDIE_REFERENCE_EXECUTABLE=/absolute/path/to/diec`. This enables
`cdie.reference`, comparing the same generated corpus and isolated databases
in both default and all-types modes. By default every output mode is checked.
Only CRLF is normalized; spaces, trailing newlines, stderr and exit codes
remain significant. Timeouts fail the test.

Reference versions matter. The local 3.21 executable matches this suite's JSON
cases, but has existing differences in XML element shape, trailing newlines
and unknown-result type casing. `-DCDIE_REFERENCE_FORMATS=j` selects the JSON
subset explicitly; it is not a claim of full output compatibility. The default
`p,j,x,c,t` comparison intentionally exposes those differences. Run it directly:

```bash
python3 tests/test_compatibility.py --cdie build/src/console/cdie \
  --reference /absolute/path/to/diec --suite reference
```

The GitHub compatibility workflow builds pinned dependencies and runs the
public regressions on Linux, Windows and macOS. Its Windows job also runs the
native GUI smoke test, including applying and cancelling expansion controls.
Large real-world corpus and decoder parity sweeps remain separate work.

### Private corpus comparison

For a repeatable comparison of the locally built console versions, use the
developer harness in `private_tests/compare_console.py`:

```powershell
python private_tests/compare_console.py --cdie F:\ownCloud_build\cdie\build\srct\src\console\cdie.exe --diec F:\ownCloud_build\die\build\srct\src\console\diec.exe --database F:\ownCloud\prepare\qt5\_mylibs\Detect-It-Easy\db --corpus F:\out_iso\out_tmp2\PE32 --sample-per-directory 1 --batch-size 8 --timeout 120 --output private_tests/comparison-results
```

It uses `-b -d -u` for both tools, selects one file per directory recursively,
and explicitly isolates extra/custom databases so only the specified main
database is used. It snapshots both executables and records database hashes,
file sizes, modification times, raw stdout/stderr, exit codes and diffs. Only
CRLF is normalized; whitespace and diagnostic messages remain significant.
Use `--files-from` with a UTF-8 newline-separated manifest for a fixed file
selection, and `--resume` to continue an interrupted run with unchanged inputs.
Timeouts are recorded as failures, never as equal empty outputs.

After rebuilding `cdie`, use a new output directory and
`--reference-results private_tests/comparison-results` to reuse successful
`diec` records. The harness verifies the reference executable, database,
auxiliary database isolation, raw output hashes, and input size/mtime before
reusing a record. Missing reference records are scanned normally. `--resume`
requires both executables to remain unchanged.

`private_tests/test_signature_errors.c` checks malformed signatures, cached
comparisons, searches, and the unmatched quote in the database's ZIP marker.
`private_tests/test_windows_argv.c` compares the Windows UTF-8 argument parser
with the CRT's wide-character parser. Both are private CMake test targets.

The private I/O-buffer suite checks the shared `xx_io` cache with a source that
returns short transfers. It verifies window boundaries, independent cursors,
sticky read errors, premature EOF, and source ownership. Automatic buffering
eagerly copies inputs smaller than the captured capacity, including a trailing
NUL, while equal-sized inputs remain streamed. Eager-load failures leave the
source open and owned by the caller. The same suite checks adaptive whole-file
promotion, borrowed memory, and search estimates with extreme capacities.
Build and run it with:

```powershell
cmake --build F:\ownCloud_build\cdie\build --target cdie_test_io_buffer
ctest --test-dir F:\ownCloud_build\cdie\build -R io_buffer --output-on-failure
```

The cache retries short reads in bounded requests and exposes a completed
window or whole-file copy only after all declared bytes are present. This matters for files
above 2 GiB: the Windows device caps a single transfer at `0x7fffffff` bytes.
Previously one read silently scanned only that prefix. Two RAR SFX inputs
exposed the problem: `TSData.exe` contains 1351 files and 178 directories, but
its first 2 GiB contain only 283 files and 142 directories; `Game.exe` contains
139 files and 36 directories, versus 98 files and 22 directories in the prefix.
These counts come from seeking between archive headers; compressed payloads
need not be unpacked to establish the expected result.

`private_tests/test_pe_metadata.c` exercises 32 small PE fixtures through the
script API: invalid-address sentinels, header entry points, unaligned raw
sections, Latin-1 section names, partial debug directories and invalid raw
pointers, ordinal/named export ordering, zero-RVA resource payloads, and the
reference console's scalar-search endianness. It also verifies reverse
physical mapping for overlapping sections and import descriptor/thunk
continuity, including the legacy partial first descriptor used by Upack.
Build the private
`cdie_test_pe_metadata` target and run its executable. The expected conditions
are independently checked against the native console by
`private_tests/verify_pe_metadata_reference.py`; its `--output` contains a
private probe database, fixtures, raw outputs and `summary.json`. The installed
database remains unchanged. For example:

```powershell
python private_tests/verify_pe_metadata_reference.py --diec F:\ownCloud_build\die\build-release\srct\src\console\diec.exe --cdie F:\ownCloud_build\cdie\build-release\srct\src\console\cdie.exe --output private_tests/pe-metadata-oracle
```

```powershell
param([string[]]$Files)

$root = "C:\path\to\Detect-It-Easy"
$cdie = "C:\path\to\cdie.exe"

$same = 0; $diff = 0
foreach ($f in $Files) {
    $a = (& "$root\diec.exe" $f 2>&1 | Out-String).Trim()
    $b = (& $cdie -D "$root\db" -E "$root\db_extra" -C "$root\db_custom" $f 2>&1 | Out-String).Trim()
    if ($a -eq $b) { $same++ } else { $diff++; "DIFF: $f" }
}
"same=$same diff=$diff"
```

Four things will produce false results if you skip them:

1. **Give `cdie` all three databases.** `diec` picks up `db`, `db_extra` and
   `db_custom` from its own directory; `cdie` needs `-D`, `-E` and `-C`.
2. **Avoid WOW64 redirection.** The shipped `diec.exe` is 32-bit, so it reads
   `C:\Windows\SysWOW64\x.dll` when you ask for
   `C:\Windows\System32\x.dll`. A 64-bit `cdie` reads the real System32 file.
   Compare inside `SysWOW64`, or use any directory that is not redirected.
3. **Copy the binary before a long sweep.** Rebuilding mid-run swaps the
   executable underneath it and the results mean nothing — silently, since
   the sweep just keeps going.
4. **Do not compare a Windows tool's output with a Linux build's directly.**
   `diec.exe` emits CRLF and a POSIX `cdie` emits LF, so every single file
   "differs". Strip `\r` from both, or compare like with like.

## Results

The following console-parity results are the pre-cdisasm baseline. The corpus
and output-mode sweeps have not yet been rerun with the cdisasm decoder.

| Corpus | Files | Identical |
| --- | --- | --- |
| `C:\Windows\SysWOW64\*.dll` | 120 | 120 |
| Visual Studio 2022 `Common7\IDE\*.dll` (mostly .NET assemblies) | 80 | 80 |
| Qt 5.15.2 `msvc2019_64\bin` | 40 | 40 |
| `C:\utils\python3` (`*.exe`, `*.dll`, `*.pyd`) | 60 | 60 |
| Detect It Easy distribution, **every** file — binaries, scripts, text, images, an APK, a PDF | 48 | 48 |
| **total** | **348** | **348** |

Separate format sweeps also match byte for byte: 40 PDFs (classic `xref`,
cross-reference streams, incrementally-updated files), 60 ELF binaries and
shared objects (glibc/GCC detection, `.comment` versions, static and dynamic),
a DEX (`classes.dex`, version + map hash + ProGuard) and a synthetic Mach-O
(dylibs, sections, entry point), plus the APK manifest extraction. ELF, Mach-O
and DEX are not in the Windows corpus above, so they are tested against
`diec.exe` on files pulled from WSL and, for Mach-O, a hand-built sample.

Deliberately point the sweep at a whole directory rather than a filtered list
of executables. Restricting it to `*.dll` and `*.exe` hides every gap that
lives outside PE parsing — that filter is what let plain-text detection, the
PNG detail line and the APK/PDF parsers stay unwritten while the sweep read
322/322.

Rounds of that sweep found two real bugs, both since fixed:

* a phantom trailing `REPRO` debug record on 31 files (see *Robustness*
  below and `parse_debug` in `format/xpe.c`);
* an access violation on an input where a signature script hands the engine
  an offset near `2^63`.

### Sweep the other output modes too

The default text output is the acceptance criterion, so it is easy to test
only that — and everything else then drifts unnoticed. Loop the same corpus
over the flags as well:

```powershell
foreach ($fl in @("-j","-x","-c","-t","-b","-p")) {
    $a = (& "$root\diec.exe" $fl $f | Out-String).Trim()
    $b = (& $cdie -D "$root\db" -E "$root\db_extra" -C "$root\db_custom" $fl $f | Out-String).Trim()
    if ($a -ne $b) { "DIFF $fl $f" }
}
```

That is what turned up the structured formats having a different shape
entirely from `diec`'s, `--verbose` printing `Windows(6.0)` where `diec`
prints `Windows(Vista)[AMD64, 64-bit]`, and — worst of the three — short
options that silently meant different things in the two tools (`-p` was
profiling here and `--plaintext` there). All are fixed; the point is that
none of them were visible from the default-output sweep.

Include an input with **no** detections, which the structured formats treat
specially: DiE drops the file-part wrapper and emits a single bare record
carrying the file type as its `string`. A few kilobytes of random bytes plus
`--hideunknown` gets you there.

```bash
cdie -D db -j -U random.bin
```

```json
{
    "detects": [
        {
            "info": "",
            "name": "",
            "string": "Binary",
            "type": "",
            "version": ""
        }
    ]
}
```


The target case from the task, `python3.dll`:

```text
PE64
    Linker: Microsoft Linker(14.50.35225)
    Compiler: Microsoft Visual C/C++(19.50.35225)[LTCG/C]
    Tool: Microsoft Visual Studio(2026, 18.0-18.3)
    Sign tool: Windows Authenticode(2.0)[PKCS #7]
    Debug data: Records[CodeView, VC Feature, POGO]
```

`--showdatabase` also matches, both the signature counts and the file type
display names:

```text
	Binary: 292     PE: 965      APK: 52       Amiga Hunk: 98
	COM: 247        ELF: 47      DEX: 29       Atari ST: 1
	MSDOS: 351      Mach-O: 12   NPM: 4        DOS/16M: 2
	NE: 13          PDF: 7       Mach-O FAT: 2 DOS/4G: 2
	LE: 3           CFBF: 3      ISO 9660: 23
	LX: 5           Image: 1     Archive: 1
	                JPEG: 5      ZIP: 3
	                PNG: 1       JAR: 2
	                RAR: 1
```

## Cross-checking the .NET metadata

Assembly and module names alone are a strong signal that the table layout is
right: the `Assembly` table sits at index `0x20`, so its file offset depends
on every preceding table's row size being computed correctly. If any coded
index or heap index width were wrong, the name would come back as garbage.

For a stricter check, dump the tables with an independent reader and feed the
names back through a throwaway probe signature:

```js
// probedb/PE/_probe.0.sg
meta("probe", "net");

function detect() {
    if (PE.isNet()) {
        _setResult("probe", "type",   PE.isNetTypePresent("Some.Namespace", "SomeType") ? "yes" : "no", "");
        _setResult("probe", "method", PE.isNetMethodPresent("Some.Namespace", "SomeType", "SomeMethod") ? "yes" : "no", "");
    }
    return result();
}
```

```bash
cdie -D probedb some_assembly.dll
```

The probe database needs `_init`, `_debug`, `_runtime_helpers`, `language`
and `PE/_init` copied from the real database — those provide `meta()` and
`result()`.

Checks worth including: a type that exists, a type that does not, a method
in the middle of a type's method range, and a method that does not exist.
Getting all four right exercises the `TypeDef` → `MethodDef` range walk,
which is where an off-by-one would hide.

## Parsing the whole database

Every script in the database should parse. `js_parse_program()` is enough to
check that:

```c
JSNode *pNode = js_parse_program(pCtx, pSource, pPath, &pError);
if (pNode == NULL) { printf("FAIL %s: %s\n", pPath, pError); }
```

Walking the stock database this way gives `ok=2098 fail=26`; all 26 failures
are `.png`, `.txt` and `.json` files inside sub-directories that the loader
ignores anyway (`_icons/`, `.vscode/`, `PE/dotnet_only/`, `PE/native_only/`).

## Testing the runtime primitives

The Windows build supplies its own `strtod`, `dtoa`, `printf` and libm
(see [ARCHITECTURE.md](ARCHITECTURE.md#the-runtime-layer)), so those are worth
testing directly rather than only through a scan. They are also where a
regression is least obvious: a broken `x_strtod` does not crash, it silently
turns `Microsoft Linker(14.50)` into `Microsoft Linker(14.32)`.

`tests/test_runtime.c` does that. It links only `core/utils*.c`, with the same
flags the scanner uses — so building it is itself a check that the CRT-free
link still works — and exits with the number of failures:

```bash
cmake -S cdie_source -B cdie_build -DCDIE_BUILD_TESTS=ON
cmake --build cdie_build
cdie_build/tests/cdie_test_runtime
```

```text
--- x_strtod ---
ok   5e-324                             5e-324
...
63 checks, 0 failures
```

Cases that actually catch things:

| Input | Why |
| --- | --- |
| `0.1 + 0.2` → `0.30000000000000004` | shortest round-trip, the classic |
| `5e-324` | smallest subnormal |
| `1.7976931348623157e+308` | largest finite; catches exponent overflow in the bignum |
| `1e21`, `1e-7` | the two thresholds where `String(number)` switches to exponential |
| `(0.5).toFixed(0)` → `"1"` | the value rounds up into a place it has no digit for |
| `(13.75).toFixed(1)` → `"13.8"` | round-half-up at the boundary |
| `(1234.5678).toPrecision(2)` → `"1.2e+3"` | ECMAScript does not zero-pad the exponent; `%g` does |
| `x_strtod` round-tripping each of the above | the two directions fail independently |
| `x_strcmp("B", "a") < 0` | byte ordering, not locale ordering |

Both directions matter and they fail independently. During development
`x_dtoa_shortest` printed `5e-324` correctly while `x_strtod` read it back as
`2.2250738585072014e-308` — the smallest *normal* — because the assembly step
picked its encoding from the exponent instead of from the mantissa. Round-trip
tests catch that; testing either direction alone does not.

Equally, `x_dtoa_shortest` passed all eighteen of its cases while `x_strtod`
was returning `32` for `"50"`, a normalisation bug in the long division that a
scan could only ever reveal as a wrong version number.

## Tracing a scan

Set `CDIE_TRACE` to print each script name to stderr just before it runs.
This is how you find the rule responsible when a scan misbehaves on an
unusual input:

```bash
CDIE_TRACE=1 cdie -D db weird.bin 2>&1 | tail -5
```

```powershell
$env:CDIE_TRACE = "1"; cdie.exe -D db weird.bin
```

## Robustness

Signature scripts are third-party code and some of them are buggy. One rule in
the stock database (`Binary/format_bin.Hermes.1.sg`) does

```js
Binary.compare("C61FBC03C103191F", Binary.read_uint64(0, _BE))
```

— passing a 64-bit big-endian *value* where an offset is expected. The engine
therefore receives an offset near `2^63`, and `nOffset + nSize` overflows.
Every range check in `format/xb.c` is written overflow-safe
(`nSize <= nFileSize - nOffset`, never `nOffset + nSize <= nFileSize`), and
`xb_clamp()` rejects out-of-range starts outright.

When adding new API functions, keep that property: never add an offset and a
size before comparing them against the file size.

Two more guards exist for the same reason: the JavaScript call depth is
capped (`JSCtx.nMaxCallDepth`) and the regular-expression matcher limits both
backtracking steps and recursion depth (`RE_MAX_STEPS`, `RE_MAX_DEPTH`), so a
runaway rule fails its own detection instead of taking the process down.

## Speed

Scanning a single PE with the full database, including loading ~2100 scripts
from disk:

| Tool | Time |
| --- | --- |
| `diec` (Qt + QtScript) | ~1340 ms |
| `cdie` | ~790 ms |

Entropy regressions run as `cdie.entropy` and optional `cdie.entropy_reference`.
They cover zero/one/eight bits per byte, empty files, every generated executable
variant, region boundaries, byte histograms, block partition remainders,
all output formats, multiple/recursive targets and invalid combinations. Linux
also analyzes a 128 MiB file under a 64 MiB address-space limit. The shared
widget probe verifies snapshot ownership, transactional rejection, 64-bit ranges,
NaN/bounds/gap validation, returning to owned-byte mode and SVG export.
Cancellation before and during I/O returns no partial model. A POSIX PTY opens
the actual worker/progress/result modal and dismisses it with Escape. Windows'
native smoke sequence also opens Entropy and requires populated region rows.
The optional DIE check compares seven PE/binary cases at 1e-12 tolerance.

Hash regressions run as `cdie.hash` and optional `cdie.hash_reference`.
They compare CRC32/MD5/SHA1/SHA256 with Python zlib/hashlib for known vectors,
empty inputs, hash-block padding boundaries and 64 KiB I/O boundaries. Generated
PE/ELF/Mach-O/PNG/ZIP files verify format-independent hashing without a database.
Coverage also includes selectors, all output formats, multiple/recursive targets,
invalid modes and algorithms. A borrowed fake device forces 17-byte short reads,
mid-stream EOF/errors, unknown sizes and failed seeks; failures return no model.
The GUI probe checks result equality, ownership, invalid model replacement,
selection, copy-text export/short buffers, clearing and cancellation. A POSIX
PTY opens the actual worker/progress/result modal and closes it with Escape.
Linux hashes a 128 MiB file under a 64 MiB address-space limit. Native Windows
and Cocoa smoke sequences check populated hash rows when run on those platforms.
The optional DIE reference checks 21 selected MD5/SHA1/SHA256 results. Older
DIE 3.21 returns blank strings for empty files; that known difference is explicitly
accepted only for an empty reference value and CDIE must match hashlib instead.

Strings regressions run as `cdie.strings`. ASCII extraction is compared with
independent byte-run matching and optionally the installed native `strings`
command. Strict Python decoders verify every emitted byte range, decoded text
and scalar count. Fixtures cover UTF-8, both UTF-16 byte orders/alignments,
surrogate pairs, partial/malformed sequences, BOMs, automatic detection,
minimum lengths, empty files and 64 KiB boundaries. Console coverage includes
all report formats, quoting, recursion, missing databases, invalid options and
record/string/aggregate limits with no partial output.

Borrowed fake devices force 17-byte short reads, mid-stream EOF/read failures,
unknown sizes and failed seeks. Worker cancellation before/during I/O exposes
no model. The GUI probe verifies console equality, copied ownership, filtering,
selection, invalid replacement rollback, CSV export/short buffers and clearing.
The CSV host callback is checked with a Unicode destination and a failed open.
A POSIX PTY opens the real worker/progress/result modal, activates full-text
View with Enter, closes it with Escape, then closes the restored parent browser.
Linux also scans a sparse 128 MiB file under a 64 MiB address-space limit.
Native Windows/Cocoa smoke code checks populated shared Strings controls using
a small synthetic record, avoiding extraction limits in the GUI executable itself.
Native file-chooser interaction and desktop runtime validation require those hosts.

Hex View regressions run as `cdie.hex_view` using the actual shared browser.
Six methods cover empty/tiny files, Unicode paths, read-only open/unchanged
input, missing files, cross-window selection export, fake-device short reads,
transactional replacement/read-failure rollback, decimal/hex/invalid offsets,
page buttons, refresh/file shrink, encoding/row changes, borrowed-device lifetime
and oversized-copy rejection without source reads. The virtual source uses a
byte pattern varying above 32 bits, so address truncation cannot pass by wrapping
a repeating pattern. Linux opens a sparse file over 4 GiB under a 64 MiB address
limit, verifies the displayed last byte against a known marker, and preserves
its size/content. A PTY opens the real CDIE adapter/modal and exercises Ctrl+G,
Go/Enter, F5 and Escape. Native Windows/Cocoa smoke code checks populated file
hex controls. Desktop runtime, native clipboard and chooser interaction remain
host-specific checks; local cross-compilation covers changed Windows sources.

Disasm regressions run as `cdie.disasm`. Eleven methods cover x86 16/32/64-bit
golden instructions; ARM, Thumb and AArch64 in both instruction byte orders;
native PE/ELF/Mach-O mode detection, mapped entry points and virtual addresses;
manual raw-file modes, empty files, missing files, cancellation, invalid/truncated
opcodes and instruction boundaries across the 4 KiB page limit. ARM cases skip
when those decoders are explicitly disabled. Linux reads a known instruction
beyond 4 GiB under a 64 MiB address-space limit.

The shared browser probe verifies copied row ownership after native page
replacement, manual mode selection, Next/Back history, Go/EOF and rollback
after failed providers or invalid pages. A PTY opens the real worker/modal,
exercises Ctrl+G, decimal Go, F5 and Escape, and verifies unchanged source bytes.
Native Windows/Cocoa smoke code requires populated shared Disasm controls;
desktop runtime and clipboard checks require those hosts. Targeted MinGW
compilation covers the changed Windows GUI, adapter, decoder and widget sources.

Memory Map regressions run as `cdie.memory_map`. Ten methods verify PE32/64
headers, sections, overlays, virtual tails and absent entry points; ELF32/64
segments and zero fill in both byte orders; and Mach-O32/64 segment maps and
LC_MAIN entries in both byte orders. Flat Data, empty files, malformed-header
fallback, missing files, cancellation before/during snapshot copying and the
1024-record bound are covered. Linux maps a sparse file beyond 4 GiB under a
64 MiB address-space limit without reading the whole file.

The actual shared browser is tested for copied string ownership, transactional
rejection of invalid ranges/flags/UTF-8/counts, clearing, 64-bit offset/address
lookup, half-open boundaries, virtual/invisible rows, first-match overlap,
escaped names, text export and short output buffers. A PTY opens the real
worker/modal, uses Ctrl+G and Enter to locate a section, closes with Escape and
checks unchanged source bytes. Native Windows/Cocoa smoke sequences require a
populated Memory Map table. Changed Windows GUI, adapter, reader, table, modal
and Win32/TUI backend sources compile with MinGW; native desktop runtime and
clipboard validation require those hosts. Cocoa/GTK sources were not compiled
locally. Existing Disasm and other analysis suites also exercise the expanded
shared table engine.

Imports regressions run as `cdie.imports`. Ten methods verify independently
generated PE32/64 normal/delay, named/ordinal, hint, physical IAT offset and
virtual IAT address values. PE32 legacy VA-based delay descriptors and absent
OriginalFirstThunk fallback are covered. ELF32/64 fixtures exercise both byte
orders and ET_REL/ET_DYN undefined global/weak filtering. Mach-O32/64 fixtures
in both byte orders verify dylib ordinals, special unresolved ordinals and
exclusion of common/local/debug/defined symbols. Empty tables, unsupported and
missing inputs, malformed thunk reads, pre/during cancellation, name escaping
and record/name/aggregate bounds require no partial result.

The real shared browser is checked for copied ownership after snapshot release,
case-insensitive filtering, visible versus total counts, no matches, reset,
invalid UTF-8/offsets/counts, rollback, clearing, escaped TSV, full 64-bit values
and short buffers. A POSIX PTY opens the actual worker/modal, focuses Filter
with Ctrl+F, types a symbol substring, observes one of four imports, closes with
Escape and verifies unchanged source bytes. Linux reads a sparse PE beyond
4 GiB under a 64 MiB address-space limit. Native Windows/Cocoa smoke sequences
require populated Imports controls. The changed Windows GUI, adapter, reader,
browser and modal compile with MinGW; native desktop runtime, clipboard and
Cocoa compilation require their respective hosts. The full scan/analysis suite
is rerun after enabling native import/symbol stream support.

Exports regressions run as `cdie.exports`. Ten methods verify generated PE32/64
named, ordinal-only, forwarded, aliased and empty function slots, with exact
physical offsets and virtual addresses. ELF32/64 fixtures in both byte orders
exercise DYNSYM/ET_REL, visibility/binding/type filtering, mapped data/functions
and absolute values with no physical offset. Mach-O32/64 fixtures in both byte
orders cover public/private/local/debug/undefined nlist symbols, indirect aliases,
absolute exports, export tries and dylib reexports. Empty/unsupported/missing
inputs, cancellation before/during reads, malformed targets/tries, escaped name/
forwarder bytes and record/name/aggregate limits require no partial model.

The actual shared browser is checked for native-adapter equality, copied source
ownership, name/forwarder filtering, visible/total counts, empty matches, reset,
invalid UTF-8/offsets/counts, rollback, clearing, escaped TSV, unknown ordinal
display, full 64-bit values, exact NUL-inclusive export sizes and short buffers.
A POSIX PTY opens the real worker/modal, focuses Filter with Ctrl+F, types a
forwarder/name substring, observes one of three exports, closes with Escape and
checks unchanged input. Linux reads a sparse PE beyond 4 GiB under a 64 MiB
address-space limit. Native Windows/Cocoa smoke code checks populated shared
Exports controls using a synthetic row because GUI executables may export no
symbols. Changed Windows GUI, adapter, reader, browser/modal and shared export
table sources compile with MinGW. Native desktop runtime/clipboard and Cocoa
compilation require those hosts. The full suite is rerun after enabling export
streams and expanding the shared export table.

Resources regressions run as `cdie.resources`. Eleven methods verify generated
PE32/64 numeric IDs, multiple languages, code pages, byte ranges, Unicode names
including a surrogate pair and zero-byte resources. ELF32/64 fixtures in both
byte orders cover bundle/leaf ranges, sorting, compressed stored bytes, empty
leaves and invalid UTF-8 key escaping. When available, `glib-compile-resources`
generates an independent bundle and the reported leaf bytes must match its
original input. This optional method skips when GLib's compiler is absent.

Tests cover empty/unsupported/missing inputs, cancellation before/during
collection, invalid PE payload ranges/directory cycles, malformed GVDB roots
and native skipped cyclic leaf-name graphs. Exactly 10,000 resources load;
record/name/aggregate overflow returns no model. Linux reads a sparse PE beyond
4 GiB under a 64 MiB address-space limit.

The shared browser probe checks every native field against the adapter, copied
ownership of all three strings, filtering/counts, empty matches, reset, invalid
UTF-8/offsets/full ranges/counts, transactional rollback, clearing, escaped TSV,
numeric IDs/code pages, exact NUL-inclusive output length and short buffers.
A POSIX PTY opens the actual worker/modal, focuses Filter with Ctrl+F, filters
by a language ID to one of two resources, closes with Escape and checks unchanged
input. Native Windows smoke opens its own PE resource table; Cocoa smoke uses
a synthetic record because Mach-O has no native resource provider. Changed
Windows GUI, adapter, collector, shared browser/modal compile with MinGW.
Native desktop runtime/clipboard and Cocoa compilation require those hosts.

Symbols regressions run as `cdie.symbols`. Ten methods cover PE32/64 COFF
primary/auxiliary records, file names, function sizes, undefined/debug/absolute
symbols, Unicode names and physical/virtual targets. ELF32/64 in both byte
orders exercises ET_REL/ET_DYN symbol tables, local/global/weak bindings,
hidden/undefined/file/section records and absolute values. Mach-O32/64 in both
byte orders covers public/private/local/debug/undefined nlist symbols, indirect
aliases, absolute symbols and trie-only entries with unknown nlist locations.

Tests cover missing/unsupported/empty inputs, cancellation before/during reads,
invalid tables, byte escaping and record/name/aggregate limits. Exactly 10,000
symbols load; a larger model fails without partial results. Linux reads symbols
from a sparse PE beyond 4 GiB under a 64 MiB address-space limit.

The real shared browser is compared against every native field, then checked
for copied ownership, name/filename filtering, visible totals, reset, invalid
offsets/record offsets/enums/UTF-8/counts, rollback, clearing, TSV escaping,
flags, full 64-bit values, EOF target offsets, exact export size and short
buffers. A direct table replacement with an oversized filename is safely
rejected by TSV export. A POSIX PTY opens the worker/modal, uses Ctrl+F to
filter to one of six symbols, closes with Escape and verifies unchanged source
bytes. Native Windows/Cocoa smoke uses a synthetic symbol because GUI binaries
may be stripped. Changed Windows GUI, adapter, reader, browser/modal compile with
MinGW; native desktop runtime/clipboard and Cocoa compilation require those hosts.
