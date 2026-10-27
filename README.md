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

## Scan expansion

The console exposes the shared engine's additional scan passes:

```bash
cdie --alltypes --overlayscan --resourcesscan --archivesscan -j sample.exe
```

`-a` / `--alltypes` adds applicable parent-format passes, such as MSDOS for a
PE executable. `--overlayscan` scans trailing overlays, `--resourcesscan`
scans PE/ELF resource payloads, and `--archivesscan` scans supported decoded
archive members and Mach-O Fat slices. Archive expansion covers ZIP-family
containers, LHA, GZIP and BZIP2; it visits one level. `-r` still controls
directory traversal independently. The console leaves all four modes off
unless requested, preserving its default output.

On Windows and macOS, **Scan options** contains the same four controls. The
TUI exposes them with **F2**. Interactive frontends initially enable overlays;
the other expansion modes start off. OK applies changes to the next scan;
Cancel preserves them. The result tree and Report use the same scan results.

Public generated-fixture tests and an optional strict `diec` comparison are
documented in [docs/TESTING.md](docs/TESTING.md#public-compatibility-tests).

## File information

File Information structure commands use native **DataStructs for PE, ELF and thin Mach-O
only**. A signature database is not required.

The Windows and macOS GUIs provide a **File Information** button for the current
file path. Windows also supports **Ctrl+I**; macOS provides **File → File
Information** with **Cmd+I**. The reusable `xxwidgets_file_information_*` view
shows structures on the left and typed fields on the right, including raw and
display values, relative/file offsets, sizes, roles and comments. Raw ranges
show their metadata. Close or Escape returns to the scan window without
changing its results. Loading runs on a worker with cancellable progress.

```bash
cdie --info program.exe
cdie --showstructs
cdie --showstructs --json program.exe
cdie --struct "PE::IMAGE_DOS_HEADER#e_lfanew" --json program.exe
cdie -S "ELF::elf_header64" -j program.elf
cdie -S "MACHO::mach_header_64" -j program.macho
```

`-i` / `--info` dumps native structures and their fields. `-S` / `--struct`
selects a native structure name, optionally qualified with `PE::`, `ELF::` or
`MACHO::`; `#FIELD` selects a named field. Names are case-sensitive. `Info` is
an alias for the complete dump, and `-S` takes precedence when combined with
`-i`. `-w` / `--showstructs` lists structures present in a target; without a
target it lists the supported native catalog.

JSON, XML, CSV, TSV and plaintext are supported. Structure offsets are absolute
file offsets; field offsets are relative to their structure. Inventory retains
native table counts, while full information expands fixed-size table entries
into separate occurrences. Raw descriptors such as DOS stubs and padding retain
their ranges and have no fabricated fields. Repeated names remain separate
records. This native DataStructs output has its own schema and is not a
byte-for-byte reproduction of Qt's XFileInfo report.

Directory targets support `-r`. Multi-file JSON/XML stays one document, and
CSV/TSV includes a path column and one header. Unsupported formats, malformed
inputs, unknown or absent structures and missing fields return an error without
printing a partial file report. File information and database operations are
separate command modes; structure inventory cannot be combined with a dump.

## Entropy

`cdie --entropy FILE` (`-e`) calculates Shannon entropy in bits per byte,
without a signature database. Use `-j`, `-x`, `-c`, `-t` or `-p` for JSON,
XML, CSV, TSV or plaintext. Directory targets and `-r` work as with scans;
multi-file JSON/XML uses a `files` wrapper. Entropy is a separate command mode
and cannot be combined with File Information, inventory or database operations.

```bash
cdie -e program.exe
cdie --entropy --json program.elf
cdie -rej samples/
```

The report includes the whole-file total, physical region offsets/sizes,
entropy and a `packed` / `not packed` status (threshold 6.5). PE headers,
sections and overlays, ELF segments and Mach-O segments come from native memory
maps. Other files use one `Data` region. Virtual-only ranges are excluded and
physical ranges are clipped to the file. Non-ASCII/control bytes in region
names are displayed as byte escapes. Empty files have entropy zero.

Both native GUIs have an **Entropy** button; Windows supports **Ctrl+E** and
macOS provides **File → Entropy** with **Cmd+E**. A cancellable worker streams
the analysis, then `xxwidgets_entropy_dialog` shows totals, regions, a byte
histogram and an entropy curve. The region filter, map selector, grid and
sample tracking reuse the existing `xxwidgets_entropy_*` controls. The graph
uses up to 100 automatic blocks, with the final block including the remainder.
Count/Reload are disabled for these precomputed results. Widgets copy only
statistics and keep no source-file bytes.

## Hash

`cdie --hash FILE` or `cdie -S Hash FILE` computes whole-file **CRC32, MD5,
SHA1 and SHA256** using the native xxfclib streaming contexts. Hashing works
for every file type and requires no signature database. `-h` remains Help.

```bash
cdie --hash program.exe
cdie -S Hash --json program.elf
cdie -S "Hash#SHA256" --json file.bin
cdie --hash -rj samples/
```

`Hash#CRC32`, `Hash#MD5`, `Hash#SHA1` and `Hash#SHA256` select a single output
field; names are case-sensitive. Hash selection is a byte operation and does
not use DataStructs. Hash, Entropy, File Information, structure inventory and
database operations are separate modes. Unsupported algorithms return an
error rather than an empty value. MD4, SHA224, SHA384 and SHA512 are currently
unavailable in this command.

Text/plaintext, JSON, XML, CSV and TSV are supported. JSON uses
`{"data":{"Hash":{"MD5":"...",...}}}`; XML uses `<data>` with nested Hash
and algorithm records. CSV/TSV begins with a `Hash` row followed by
algorithm/value rows. Multiple file results use the existing JSON/XML `files`
wrapper; delimited output repeats the Hash block for each file. All digests
are lowercase hexadecimal. CRC32 is the ISO-HDLC/ZIP checksum with 8 digits.
Empty files return the standard empty-file digests (older DIE 3.21 emits blank
strings). Read errors and cancellation discard the entire file result.

The native GUIs provide a **Hash** button for the current path, **Ctrl+H** on
Windows and **File → Hash** / **Cmd+Shift+H** on macOS. A cancellable worker
calculates every digest in one pass using a 64 KiB buffer. The reusable
`xxwidgets_hash_*` browser copies only path, size and hash strings. Select a
row to inspect its digest; **Copy selected** copies that value and **Copy all**
copies algorithm/value lines. Close or Escape returns to the scan window.

## Strings

`cdie --strings FILE` (also `-S Strings`) extracts string candidates from any
file type without a signature database or DataStructs. The default minimum is
**4 decoded characters**, with ASCII/UTF-8 and automatic UTF-16LE/BE scanning.
`-s` continues to mean database information.

```bash
cdie --strings program.exe
cdie --strings --strings-min 8 --strings-encoding ascii -j file.bin
cdie -S Strings --strings-encoding utf16le -j file.bin
cdie --strings -rj samples/
```

`--strings-min` accepts 1–1024; `--strings-encoding` accepts `ascii`, `utf8`,
`utf16le`, `utf16be` or `all`. These options require Strings mode. ASCII means
bytes 0x20–0x7E. UTF-8 decoding is strict; ASCII-only runs retain the `ASCII`
label. UTF-16 checks both byte alignments and decodes surrogate pairs. In
`all` mode a UTF-16 run needs a BOM or a consecutive ASCII run at least as long
as the minimum, reducing accidental wide interpretations of binary data. Use
an explicit UTF-16 encoding for text without a BOM or that ASCII anchor.
Controls, BOMs, malformed sequences and noncharacters delimit candidates;
this uses a scalar acceptance policy rather than a Unicode category database.

Records are sorted by physical file offset, then encoding. They include
`offset`, `byte_size`, `character_length`, `encoding` and decoded UTF-8 `text`.
Sizes exclude terminators/BOMs, and character counts count Unicode scalars.
Different encodings may describe overlapping byte ranges. All five console
report formats are supported. JSON uses `{path,size,strings:[...]}`; XML uses
`<file-strings>` and `<string>` records; CSV/TSV has a path column and one header
across multiple files. Recursive targets use `-r`; multi-file JSON/XML uses
the usual `files` wrapper. DIE 3.21 has no matching Strings command/schema.

The native GUIs provide **Strings**, with **Ctrl+Shift+S** on Windows and
**File → Strings / Cmd+Shift+S** on macOS. Loading runs on a cancellable worker
using the default extraction settings. The shared `xxwidgets_strings_dialog`
reuses its offset/size/encoding table, filter, selection and Copy controls.
**View** or Enter opens full decoded text; **Save CSV** exports visible rows
after choosing a native destination. The host writes the CSV outside input
dispatch. Save is hidden when no native chooser is available. Close/Escape
restores the scan window; closing View first returns to the Strings browser.

Extraction reads in 64 KiB chunks and retains at most 10,000 results, 8 MiB of
UTF-8 text and 1 MiB per decoded string. Console reports are capped at 32 MiB.
A limit, read error or cancellation fails the file operation without exposing
partial results. Empty files produce an empty list. Strings, Hash, Entropy,
File Information and database operations are separate command modes.

## Hex View

The Windows and macOS GUIs provide **Hex View** for the current file path,
with **Ctrl+Shift+X** on Windows and **File → Hex View / Cmd+Shift+X** on macOS.
It opens any file type read-only, including empty files, without a signature
database or DataStructs. The existing `xxwidgets_hexview_*` control supplies
hex bytes, addresses, decoded symbols, selection and keyboard/mouse navigation.

The new reusable `xxwidgets_hexview_browser_*` adds:

- **Go** to a decimal offset or `0x`-prefixed hexadecimal offset. Ctrl+G/Cmd+G
  focuses the field; Enter loads the position. EOF is a valid empty window.
- **Previous / Next** to move between 64 KiB window boundaries.
- **Refresh / F5** to reload the displayed bytes, including file size changes.
- ASCII, UTF-8, UTF-16LE and UTF-16BE display, with 8/16/24/32/48/64-byte rows.
- **Copy hex / Copy text**, existing context actions and streamed **Save
  selection** through the shared native destination chooser.
- **Close / Escape** to return to the scan window.

The viewer retains only a 64 KiB window, supports physical offsets beyond
4 GiB, and defers toolbar I/O/copy/export until after input dispatch. Invalid
offsets and failed reads preserve the preceding bytes. Clipboard byte/text
copies are limited to a 1 MiB selection; copying an offset needs no byte read.
Save selection can export larger ranges in chunks. The input device is opened
`rb`; editing, insertion and deletion are unavailable. There is no new console
command in this change.

## Disasm

The Windows and macOS GUIs provide **Disasm** for the current file path,
with **Ctrl+Shift+A** on Windows and **File → Disasm / Cmd+Shift+A** on macOS.
It opens files read-only without a signature database or DataStructs. The
existing `cdisasm` decoder supplies instructions; the expanded xxwidgets library
composes its existing `XXWIDGETS_DEBUGGER_DISASM` table into a reusable browser
and modal.

- **Auto (file)** detects x86 16/32/64-bit, ARM, Thumb or AArch64 from native
  PE, ELF and thin Mach-O metadata and starts at a mapped entry point when
  available. Raw files and other architectures need a manual instruction mode.
- **Mode** offers x86 16/32/64-bit and ARM32/Thumb/ARM64 in both instruction
  byte orders. Big-endian ARM files require manual selection because container
  endianness alone does not determine instruction endianness. ARM modes are
  enabled by default; build with `-DCDIE_DISASM_ARM=OFF` for x86 only.
- **Go** accepts decimal or `0x` hexadecimal physical file offsets, including
  EOF. **Ctrl+G/Cmd+G** focuses that field. Addresses in the table use virtual
  addresses where mapped, with the physical offset shown in each row's comment.
- **Next / Back** preserve decoded instruction boundaries and keep up to 256
  previous page starts. Go or changing mode clears that history.
- **Refresh / F5** reloads bytes and file size. **Copy row** copies the selected
  address, bytes, instruction and comment. **Close / Escape** returns to scanning.

Pages contain at most 512 rows whose starts lie in a 4 KiB range, with 15 bytes
of lookahead to complete the last instruction. Invalid, unsupported or truncated
instructions become single-byte `db` rows. Opening metadata runs on a cancellable
worker; bounded page reads are deferred until after input dispatch. Invalid
offsets and failed reads preserve the preceding page. Reopen the viewer to
redetect headers after a file changes. This is linear disassembly; symbols,
control-flow analysis, execution and editing are outside its current scope.

## Memory Map

The Windows and macOS GUIs provide **Memory Map** for the current file path,
with **Ctrl+M** on Windows and **File → Memory Map / Cmd+Shift+M** on macOS.
It uses the native default map: sections for PE, segments for ELF and thin
Mach-O. Other inputs, including Mach-O Fat, use one unmapped physical Data
region; empty raw files have no regions. No signature database or DataStructs
is required, and the input is opened read-only.

The reusable `xxwidgets_memory_map_*` browser and modal expand the existing
shared tabular controls with **File offset, Address, Size, Type, Name and
Attributes** columns. Metadata includes the file size, map mode, image base and
entry address. Unknown addresses and virtual-only file offsets display `-`.
Headers, overlays, virtual-only tails and invisible records are retained;
attributes describe physical/virtual/invisible status, not access permissions.
Unusual name bytes are escaped as `\xNN`.

**Find** accepts decimal or `0x` hexadecimal values. **File offset** selects the
first physical region containing the value; Enter performs the same lookup.
**Address** includes virtual-only regions. Ranges are half-open, and the first
record wins on overlap. Invalid/unmatched values preserve selection.
**Ctrl+G/Cmd+G** focuses the lookup field. **Copy row / Copy all** produces
tab-separated text with escaped single-line cells, and **Close / Escape**
returns to the scanner.

Loading runs on a cancellable worker and publishes an owned metadata snapshot
of at most 1024 records. Invalid ranges, cancellation and excessive record
counts produce no partial model. The browser copies its input and owns no
device or parser. Reopen it to reflect changed file headers. Alternate mapping
modes, process-memory maps and access permissions are outside this release.

## Imports

The Windows and macOS GUIs provide **Imports** for the current file path,
with **Ctrl+Shift+I** on Windows and **File → Imports / Cmd+Shift+I** on macOS.
Loading uses native PE, ELF and thin Mach-O readers on a cancellable worker,
with a read-only file device and no signature database or DataStructs.

- **PE** shows normal and delay-load imports by name or ordinal, including
  hints and the physical offset/virtual address of each IAT slot.
- **ELF** shows named undefined global/weak symbols from dynamic symbol tables,
  or symbol tables in relocatable objects. Library and slot locations remain
  unknown because that symbol metadata does not identify them.
- **Mach-O** shows undefined external nlist symbols, with the dylib basename
  when its library ordinal identifies one. Common, local, debug and defined
  symbols are excluded. Slot locations remain unknown.

The expanded `xxwidgets_imports_*` browser and modal reuse the existing
`XXWIDGET_IMPORTSVIEWER` table with **Library, Name, Ordinal, Address and Kind**
columns. Selection details add the file offset and hint (meaningful for named
PE imports). Unknown addresses/offsets display `-`.

**Filter** matches substrings in library or symbol names, ignoring ASCII case;
**Reset** restores all rows. **Ctrl+F/Cmd+F** focuses Filter. **Copy row** and
**Copy visible** export complete selected/filtered rows as tab-separated text,
including hint and file offset. **Close / Escape** returns to the scanner.

The owned snapshot retains at most 10,000 imports, 16 KiB per displayed name
and 8 MiB of aggregate names. Control/non-ASCII executable name bytes display
as `\xNN`; no name is silently truncated. Malformed tables, cancellation and
exceeded bounds return no partial result. Supported files without imports show
an empty table. Other formats and Mach-O Fat containers report unsupported.
Reopen the viewer after a file changes. ELF sectionless dynamic tables and
Mach-O dyld bind/chained-fixup imports are outside the current symbol-based view.

## Exports

The Windows and macOS GUIs provide **Exports** for the current file path,
with **Ctrl+Shift+E** on Windows and **File → Exports / Cmd+Shift+E** on macOS.
It loads native PE, ELF and thin Mach-O metadata on a cancellable worker,
using a read-only device without a signature database or DataStructs.

- **PE** shows named, ordinal-only and forwarded exports in function-slot
  order. Empty address-table slots are skipped. If several names alias one
  slot, the native reader returns its first name. A forwarder's displayed
  location identifies its forwarder string.
- **ELF** shows named, defined global/weak symbols with default or protected
  visibility, from DYNSYM or from SYMTAB in relocatable objects. File, section,
  local, undefined, hidden and internal symbols are excluded. Absolute symbols
  have a value/address and no physical offset.
- **Mach-O** shows exports from nlist symbols and native export tries,
  including indirect aliases, reexports and absolute values. When a trie is
  present, its public export list determines which symbols are exported.

The reusable `xxwidgets_exports_*` browser and modal expand the existing
`XXWIDGET_EXPORTSVIEWER` with **Name, Ordinal, Address and Forwarder** columns,
file metadata and selection details including physical offset. Unknown offsets,
addresses and non-applicable ordinals display `-`.

**Filter** matches substrings in name or forwarder with ASCII case folding;
**Reset** restores all rows. **Ctrl+F/Cmd+F** focuses Filter. **Copy row** and
**Copy visible** produce full tab-separated values including physical offsets.
**Close / Escape** returns to scanning. Filtering and clipboard work are deferred
until after input dispatch; replacement failures retain the previous snapshot.

Limits are 10,000 records, 16 KiB per displayed name/forwarder and 8 MiB of
aggregate NUL-inclusive text. Control/non-ASCII executable name bytes display
as `\xNN`. Failed, cancelled or over-limit reads expose no partial result.
Supported files without exports show an empty table; other inputs and Mach-O
Fat report unsupported. Reopen after file changes. ELF sectionless dynamic
tables and symbol version metadata are outside this release.

## Resources

The Windows and macOS GUIs provide **Resources** for the current file path,
with **Ctrl+Shift+U** on Windows and **File → Resources / Cmd+Shift+U** on macOS.
It loads native PE resources and ELF GResource metadata on a cancellable worker,
using a read-only device without a signature database or DataStructs.

PE records preserve numeric or named type/name/language identifiers, code pages
and physical byte ranges, including empty resources. ELF enumerates recognized
`.gresource.*` bundles and their supported value leaves. Bundle rows describe
the complete stored bundle; leaf rows describe stored data bytes. Compressed
GResource leaves retain compressed offsets/sizes. Their bytes are not decoded.

The reusable `xxwidgets_resources_*` browser and modal expand the existing
`XXWIDGET_RESOURCESVIEWER` table with **Type, Name, Language, Size and Offset**
columns, copied file metadata, visible/total counts and selection details
including all three numeric IDs and the code page. Unknown values display `-`.
Valid UTF-8 names, including PE Unicode names, are retained; control/invalid
UTF-8 bytes display as `\xNN`.

**Filter** matches type, name or language text, using decimal IDs when names
are absent and ignoring ASCII case. **Reset** restores all rows, and
**Ctrl+F/Cmd+F** focuses Filter. **Copy row / Copy visible** produces complete
tab-separated metadata, including IDs and code pages. **Close / Escape**
returns to scanning. The viewer provides read-only metadata browsing; payload
preview, extraction and editing are outside this release.

Snapshots are limited to 10,000 records, 16 KiB per displayed name and 8 MiB
of aggregate NUL-inclusive text. Failed, cancelled and over-limit loads expose
no partial model. Supported PE/ELF files without resources show an empty table;
other formats, including Mach-O, report unsupported. Native GResource parsing
can omit unsupported values or invalid leaf names while retaining the bundle
row. Reopen after file changes.

## Symbols

The Windows and macOS GUIs provide **Symbols** for the current file path,
with **Ctrl+Shift+Y** on Windows and **File → Symbols / Cmd+Shift+Y** on macOS.
It loads native PE, ELF and thin Mach-O symbols on a cancellable worker using a
read-only device, without a signature database or DataStructs.

- **PE** enumerates COFF primary symbols, preserving table indices, native
  types/storage classes, section numbers and auxiliary counts. File auxiliary
  records provide their source filename; function/section auxiliary sizes are
  shown when available.
- **ELF** enumerates native symbol tables, including local, global, weak,
  undefined, hidden and absolute symbols. Symbols from different tables remain
  separate; their record offsets distinguish them.
- **Mach-O** shows native nlist symbols and trie-only export symbols, preserving
  native values, section/type/description fields and debug/undefined status.
  Trie-only symbols have no nlist index or record offset.

The reusable `xxwidgets_symbols_*` browser and modal expand the existing
`XXWIDGET_SYMBOLSVIEWER` table with **Name, Value, Size, Type and Binding**
columns, file metadata and selection details. Details show target address,
physical offset, symbol-record offset, table index, section and status flags.
Values follow native symbol conventions; unknown offsets and addresses display
`-`. Size zero can mean unavailable, and absolute values have no file offset.

**Filter** matches name or source filename with ASCII case folding; **Reset**
restores all rows. **Ctrl+F/Cmd+F** focuses Filter. **Copy row / Copy visible**
produces complete tab-separated metadata, including fields outside the table.
**Close / Escape** returns to scanning. Filtering and clipboard work run after
input dispatch. Valid UTF-8 names are retained; control and invalid UTF-8 bytes
display as `\xNN`.

Snapshots are limited to 10,000 records, 16 KiB per displayed name and 8 MiB
of aggregate NUL-inclusive names. Failed, cancelled and over-limit loads expose
no partial model. Supported files without symbols show an empty table; raw files
and Mach-O Fat report unsupported. Reopen after file changes. Separate debug
files, DWARF/PDB decoding and ELF sectionless dynamic tables are outside this
release.

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

## Linux GUI

Install the GTK 3 development files, then configure with `-DCDIE_BUILD_GUI=ON`
and build the `cdie_gui` target. Run `src/gui/cdie_gui` from the build directory,
optionally passing a file path. Browse or drop a file to scan it; the shared
inspection buttons include File Information, Entropy, Hash, Strings, Hex View,
Disasm, Memory Map, Imports, Exports, Resources and Symbols. F5 scans, Ctrl+O
opens a file, Ctrl+R shows the report and Ctrl+Shift+Y opens Symbols.
**Database paths** (F3) edits or browses the main, extra and custom signature
folders. Empty paths use the default folders; Defaults clears all three entries.
OK reloads the databases and refreshes the tags and results. A failed main
database load keeps the preceding databases and leaves the form open for retry;
Cancel preserves them. The GUI also opens without a database so F3 can locate it.
**Fonts** (F4) reuses the shared font options form for controls, tables, trees and
text edits, with native GTK font pickers and per-role defaults. OK applies the
choices to the current session; Cancel preserves the active fonts. Database
paths and fonts are session settings.

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
| `-a`, `--alltypes` | scan applicable parent formats |
| `--overlayscan`, `--resourcesscan`, `--archivesscan` | enable bounded child scans |
| `--strings`, `-S Strings` | extract ASCII/UTF-8 and UTF-16 string candidates |
| `--strings-min`, `--strings-encoding` | set minimum length / decoding for Strings mode |
| `--hash`, `-S Hash` | whole-file CRC32, MD5, SHA1 and SHA256 |
| `-e`, `--entropy` | whole-file and region Shannon entropy |
| `-i`, `--info` | dump PE/ELF/Mach-O native DataStructs |
| `-S`, `--struct <name>` | select a native structure or `NAME#FIELD` |
| `-w`, `--showstructs` | list present structures, or the catalog without a target |
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
| `--includetags`, `--include-tags <list>` | run signatures having any listed tag; separate tags with `;` |
| `--excludetags`, `--exclude-tags <list>` | skip signatures having any listed tag; separate tags with `;` (exclusion wins) |
| `-s`, `--showdatabase` | print database paths and signature counts |

Output formats are mutually exclusive and may be selected only once. JSON and
XML remain single documents when several files are scanned; the multi-file
form records each path with its scan result. They also cover version/help,
database reports and database TAR creation without mixing plain text into
standard output.

`diec`'s `-e`/`--entropy`, `--test` and `--createtest` are not ported; see
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

## GUI/TUI translations

Start `cdie_gui` or `cdie_tui` with `--translation FILE.po`, or set
`CDIE_TRANSLATION`, to translate the interface using a UTF-8 PO catalog shared
with xxwidgets. A partial German catalog is in `translations/de.po`; English
is the default and fallback. See [translation support](docs/TRANSLATION.md)
for catalog editing, packaging and tests.
