# The signature database

`cdie` reads the stock Detect It Easy database unchanged. No conversion, no
index, no cache.

## Layout

```text
db/
├── _init                      framework script, file type "unknown"
├── _debug                     helper, included by _init
├── _runtime_helpers           String/Array polyfills, included by _init
├── language                   _setLang helper, included by _init
├── read                       byte readers, included by Binary/_init
├── FASM, FPC, Borland, ...    include-only helper scripts
├── PE/
│   ├── _init                  PE framework: PE.section[], PE.resource[], ...
│   ├── _PE.0.sg
│   ├── _linkers.6.sg
│   ├── _Microsoft.6.sg
│   ├── sign_tool_Windows_Authenticode.7.sg
│   └── ...
├── Binary/
├── ELF/
├── MACH/
└── ...
```

A **directory** maps to a file type; files directly in the database root have
file type *unknown* and are only reachable through `includeScript()`.

Recognised directories: `Binary`, `COM`, `Archive`, `ZIP`, `JAR`, `APK`,
`IPA`, `NPM`, `MACHOFAT`, `DEX`, `MSDOS`, `LE`, `LX`, `NE`, `PE`, `ELF`,
`MACH`, `DOS16M`, `DOS4G`, `Amiga`, `AtariST`, `JavaClass`, `PYC`, `PDF`,
`CFBF`, `Image`, `JPEG`, `PNG`, `RAR`, `ISO9660`.

A file is a signature when it is a regular file whose extension is `sg` or
empty. Sub-directories inside a format directory (`PE/dotnet_only`,
`PE/native_only`, `_icons`, `.vscode`) are **not** scanned — the reference
loader is non-recursive, and `cdie` matches that.

## Load order and priority

Three databases are loaded in sequence: main (`-D`), extra (`-E`), custom
(`-C`). **Each is sorted on its own and appended after the previous one.**
The combined list is never re-sorted.

Within one database the sort key is:

1. file type (enumeration order — "unknown" first, then `Binary`, `COM`,
   `MSDOS`, …, `PE`, …);
2. the **priority digit**: for a name with more than one dot, the segment
   before the extension (`_Microsoft.6.sg` → `6`,
   `compiler_EP.MSVC.4.sg` → `4`); otherwise `9`;
3. the file name.

This ordering is load-bearing. Scripts such as `_Microsoft.6.sg` guard their
output with `if (!_isResultPresent("compiler", name))`, so running them in a
different order changes what is printed.

## Execution rules

`_init` scripts never run through the normal loop. Before the loop the driver
evaluates:

1. the database-root `_init` (file type unknown), then
2. the `_init` of the detected file type.

The last match in list order wins for each, so an extra database can override
the framework.

A signature runs when all of the following hold:

* `xft_check(signature type, file type)` — a `PE` entry matches `PE32`/`PE64`,
  an `ELF` entry matches `ELF32`/`ELF64`, a `MACH` entry matches
  `MACHO32`/`MACHO64`, otherwise the types must be equal. Note that a
  `Binary` entry does **not** match a `PE64` file;
* the name's first dot-segment, uppercased, is not `DS` or `EP` unless
  `--deepscan` is given;
* that same prefix is not `HEUR` unless `--heuristicscan` is given;
* the name is not `_init`;
* the owning database is enabled.

Each script is evaluated in the global scope, then its `detect()` function is
called with `(bShowType, bShowVersion, bShowInfo)`.

## Signature syntax

`PE.compare()`, `PE.compareEP()`, `PE.findSignature()` and friends take a
signature string. `xb_convert_signature` normalises it first: text between
single quotes becomes hex, `?` becomes `.`, spaces are dropped and hex digits
are lowercased.

| Token | Meaning |
| --- | --- |
| `4d5a` | literal bytes |
| `'MZ'` | ASCII text, converted to bytes |
| `..` | skip one byte (wildcard) |
| `??` | same as `..` |
| `**` | one byte, must not be zero |
| `%%` | one printable ASCII byte |
| `%&` | one ASCII digit |
| `!%` | one non-printable byte |
| `_%` | one byte that is neither printable nor zero |
| `$$$$$$$$` | relative offset: read a signed displacement of *n*/2 bytes and jump |
| `########` | absolute address: read an address of *n*/2 bytes and jump |
| `#[base]##` | as above, with a base address |
| `++` | search forward for the following bytes, window `32 × n` |

Example, the Microsoft linker stub check from `_linkers.6.sg`:

```js
PE.compare("'MZ'90000300000004000000FFFF0000B8000000000000004000...")
```

and a relative jump from `sfx_Zip_SFX.2.sg`:

```js
PE.compareEP("e8$$$$$$$$8bff558bec83ec..a1........8365....8365....5357bf........bb")
```

## Result records

`_setResult(type, name, version, options)` appends a record. The scan driver
sorts records by the type priority:

| Priority | Types |
| --- | --- |
| 10 | operation system, virtual machine |
| 12 | format |
| 14 | platform, DOS extender |
| 20 | linker |
| 30 | compiler |
| 40 | language |
| 50 | library |
| 60 | tool, PE tool, sign tool, APK tool |
| 70 | protector, cryptor, crypter, virus, malware, trojan, corrupted data, personal data, author |
| 80 | .NET/APK/JAR obfuscator |
| 90 | dongle protection, protection |
| 100 | packer, .NET compressor |
| 110 | joiner |
| 120 | SFX, installer |
| 200 | debug data |
| 1000 | anything else |

The sort is stable, so records with equal priority keep the order in which the
scripts produced them.

A leading `~` marks a heuristic type and prints `(Heur)`; a leading `!` marks
an "aggressive heuristic" type and prints `(A-Heur)`.
