cmake_minimum_required(VERSION 3.16)

# Let per-target MSVC_RUNTIME_LIBRARY choose the CRT (the library targets pick
# the dynamic CRT so they link cleanly against default-built consumers).
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0184)
    # Select per-target runtime checks before C is first enabled. CRT-free
    # targets clear this property instead of inheriting Debug's /RTC flags.
    cmake_policy(SET CMP0184 NEW)
endif()
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/release_version.txt CDIE_VERSION)

project(cdie VERSION ${CDIE_VERSION} LANGUAGES C)

set(X_COMPANYNAME "NTInfo")
set(X_DESCRIPTION "Detect It Easy console engine (C port)")
set(X_HOMEPAGE    "http://ntinfo.biz")
set(X_MAINTAINER  "horsicq@gmail.com")

# ---------------------------------------------------------- language + options
#
# The C standard and the warning policy live here rather than in src/ because
# lib/ and tests/ are siblings of src/, so a directory-scoped setting there
# would leave the library and test targets compiling cdie's remaining sources
# as GNU C17/C23 with no warning flags at all.
#
# The warning flags themselves are applied further down, after xxfclib has been
# added -- see the note there.

# Compile only the optional format streams used by this application.
if(NOT TARGET xxfclib::xxfclib)
    set(XXFC_ENABLE_STREAM_DATASTRUCTS ON)
    set(XXFC_ENABLE_STREAM_IMPORT ON)
    set(XXFC_ENABLE_STREAM_EXPORT ON)
    # Resource expansion uses the native PE/ELF resource stream readers.
    set(XXFC_ENABLE_STREAM_RESOURCES ON)
    set(XXFC_ENABLE_STREAM_METADATA OFF)
    # ELF imports/exports enumerate symbols through the native cursor.
    set(XXFC_ENABLE_STREAM_SYMBOLS ON)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CDIE_WERROR "Treat compiler warnings as errors" OFF)

option(CDIE_BUILD_CONSOLE "Build the command-line scanner" ON)
option(CDIE_BUILD_TUI "Build the interactive terminal scanner" ON)
set(_CDIE_GUI_DEFAULT OFF)
if(WIN32 OR APPLE)
    set(_CDIE_GUI_DEFAULT ON)
endif()
option(CDIE_BUILD_GUI "Build the native desktop scanner" ${_CDIE_GUI_DEFAULT})
unset(_CDIE_GUI_DEFAULT)
if(CDIE_BUILD_GUI AND NOT (WIN32 OR APPLE OR (UNIX AND NOT APPLE)))
    message(FATAL_ERROR "The cdie native GUI requires Windows, macOS, or a GTK desktop")
endif()

# The comprehensive private Radare2 gate covers each local library with a
# compatible upstream corpus: decoding/assembly, bytecode, demangling,
# emulation and file formats. The older ASM-only switch remains available for
# callers that use the focused dependency check.
option(CDIE_BUILD_PRIVATE_RADARE2_TESTS
    "Build all private Radare2-derived compatibility tests" OFF)
option(CDIE_BUILD_PRIVATE_ASM_TESTS
    "Build only the private ARM/x86 asm corpus test" OFF)

if(CDIE_BUILD_PRIVATE_ASM_TESTS OR CDIE_BUILD_PRIVATE_RADARE2_TESTS)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/private_tests/CMakeLists.txt")
        message(FATAL_ERROR
            "Private Radare2 tests need private_tests/CMakeLists.txt")
    endif()
    enable_testing()
    set(XXFC_CDISASM_ENABLE_ARM ON)
    set(XXFC_CDISASM_ENABLE_EXTRA ON)
endif()

if(CDIE_BUILD_PRIVATE_RADARE2_TESTS)
    if(NOT DEFINED XXFC_RADARE2_TESTBINS OR
       NOT IS_DIRECTORY "${XXFC_RADARE2_TESTBINS}")
        message(FATAL_ERROR
            "CDIE_BUILD_PRIVATE_RADARE2_TESTS requires "
            "-DXXFC_RADARE2_TESTBINS=<pinned radare2-testbins checkout>")
    endif()

    get_filename_component(_CDIE_DEFAULT_PRIVATE_LIBS_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../_mylibs" ABSOLUTE)
    set(CDIE_PRIVATE_LIBS_DIR "${_CDIE_DEFAULT_PRIVATE_LIBS_DIR}"
        CACHE PATH "Directory containing the local libraries' private Radare2 tests")
    unset(_CDIE_DEFAULT_PRIVATE_LIBS_DIR)

    # Configure the format suite in xxfclib's own directory so its private
    # tests retain the correct project source directory for internal headers.
    set(XXFC_BUILD_RADARE2_PRIVATE_TESTS ON)
endif()

# The GUI disassembler uses the existing x86 and ARM/Thumb/AArch64 decoders.
option(CDIE_DISASM_ARM "Include ARM/Thumb/AArch64 in the GUI disassembler" ON)
if(NOT (CDIE_BUILD_PRIVATE_ASM_TESTS OR CDIE_BUILD_PRIVATE_RADARE2_TESTS))
    set(XXFC_CDISASM_ENABLE_ARM ${CDIE_DISASM_ARM})
endif()

# ------------------------------------------------------------------ xxfclib --
#
# Added HERE, above the warning block, on purpose. add_compile_options() and
# add_compile_definitions() are directory properties: a subdirectory inherits a
# snapshot taken at the add_subdirectory() call, so anything set after this
# point cannot reach xxfclib. Added below the block instead, every xxfclib TU
# compiles as "/W3 /W4" (211 D9025 warnings) and, with CDIE_WERROR=ON, as
# "/W3 /WX /W4" -- cdie's warning policy would then govern a library cdie does
# not own, and on GCC the trailing -Wextra would undo cdie's
# -Wno-unused-parameter as well. xxfclib sets its own /W4 (or -Wall -Wextra
# -Wpedantic), its own C11, and its own _CRT_SECURE_NO_WARNINGS, so it loses
# nothing by being configured first.
#
# Whether the console links without the CRT also has to be settled BEFORE
# xxfclib is configured, because it decides how the archive itself must be
# compiled: a /NODEFAULTLIB program cannot link objects that reference
# __GSHandlerCheck or carry /DEFAULTLIB records. x86 additionally needs the
# compiler's 64-bit arithmetic helpers (_alldiv, _allshr and friends) from
# libcmt. The in-tree compiler helpers cover x64 and ARM64, so those are the
# two architectures where this defaults to ON.
set(CDIE_NO_CRT_SUPPORTED FALSE)
if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|ARM64)$")
    set(CDIE_NO_CRT_SUPPORTED TRUE)
    set(CDIE_NO_CRT_DEFAULT ON)
else()
    set(CDIE_NO_CRT_DEFAULT OFF)
endif()
option(CDIE_NO_CRT "Link without the C runtime (KERNEL32 import only)" ${CDIE_NO_CRT_DEFAULT})

if(CDIE_NO_CRT AND MSVC AND NOT CDIE_NO_CRT_SUPPORTED)
    message(FATAL_ERROR
        "CDIE_NO_CRT is supported only for MSVC x64 and ARM64 targets")
endif()

# Older CMake versions and previously configured build trees can carry MSVC's
# /RTC flags in the configuration flags. Strip them for CRT-free targets; with
# CMP0184 NEW, the per-target MSVC_RUNTIME_CHECKS property below prevents them
# from being added again.
if(CDIE_NO_CRT AND MSVC)
    foreach(_cdie_flags CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELWITHDEBINFO)
        string(REGEX REPLACE "[/-](RTC[1csu]|GZ)" ""
            ${_cdie_flags} "${${_cdie_flags}}")
    endforeach()
endif()

if(CDIE_NO_CRT AND MSVC)
    # xxfclib's matching switch compiles the static archive /GS- /GL- /Zl and
    # exposes the architecture's clean-room compiler helpers. It deliberately
    # does NOT define
    # XXFC_NO_CRT -- that would make xxfclib's own entry object emit
    # memset/memcpy/memmove/_fltused, which src/core/utils_entry.c already
    # owns here.
    set(XXFC_STATIC_NO_CRT ON CACHE BOOL "" FORCE)
else()
    # Forced both ways: without this, flipping CDIE_NO_CRT to OFF in an
    # existing build directory leaves XXFC_STATIC_NO_CRT=ON in the cache, and
    # the archive keeps building /GS- /Zl for a build that asked to be hosted.
    set(XXFC_STATIC_NO_CRT OFF CACHE BOOL "" FORCE)
endif()

# cdie's engine lives in xxfclib. The static archive is used so the console
# keeps shipping as a single self-contained executable, which is also what the
# CRT-free configuration needs.
#
# EXCLUDE_FROM_ALL suppresses the subdirectory's install() rules. Without it
# cdie's install tree -- and therefore the CPack portable archive -- gains 207
# extra entries: the whole include/xxfclib/ header tree, lib/xxfclib.lib, and
# xxfclib's own README.md/LICENSE/FIND_TEST_RESULTS.md at the package root,
# where the first two collide with cdie's.
if(NOT TARGET xxfclib AND NOT TARGET xxfclib_static)
    set(_CDIE_DEFAULT_XXFCLIB_DIR "${CMAKE_CURRENT_LIST_DIR}/dep/xxfclib")
    if(NOT EXISTS "${_CDIE_DEFAULT_XXFCLIB_DIR}/CMakeLists.txt")
        set(_CDIE_DEFAULT_XXFCLIB_DIR "${CMAKE_CURRENT_LIST_DIR}/dep/xxfclib")
    endif()
    set(XXFCLIB_DIR "${_CDIE_DEFAULT_XXFCLIB_DIR}"
        CACHE PATH "Location of the xxfclib source tree")
    if(NOT EXISTS "${XXFCLIB_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "cdie needs xxfclib, which supplies the scan engine. Looked in "
            "'${XXFCLIB_DIR}'. Set -DXXFCLIB_DIR=<path> to point at it.")
    endif()
    add_subdirectory("${XXFCLIB_DIR}" "${CMAKE_BINARY_DIR}/xxfclib" EXCLUDE_FROM_ALL)

    # File Information deliberately exposes native DataStructs for these three
    # executable families only. Keep the focused engine independent of the
    # generic schema catalog (which is not shipped by every public mirror) and
    # unrelated archive/.NET DataStructs providers. Other scan capabilities,
    # including archive and resource scanning, keep their core readers.
    if(TARGET die_engine)
        get_target_property(_cdie_engine_sources die_engine SOURCES)
        set(_cdie_info_sources)
        foreach(_source IN LISTS _cdie_engine_sources)
            if(_source MATCHES "/xx_.+_datastructs\\.c$" AND
               NOT _source MATCHES "/xx_(pe|elf|macho|stream)_datastructs\\.c$")
                continue()
            endif()
            list(APPEND _cdie_info_sources "${_source}")
        endforeach()
        set_property(TARGET die_engine PROPERTY SOURCES "${_cdie_info_sources}")
        get_target_property(_cdie_engine_definitions die_engine COMPILE_DEFINITIONS)
        set(_cdie_info_definitions)
        foreach(_definition IN LISTS _cdie_engine_definitions)
            if(_definition MATCHES "^XXFC_STREAM_HAS_(.+)=")
                set(_cdie_stream_name "${CMAKE_MATCH_1}")
                if(_cdie_stream_name MATCHES "^(PE|ELF|MACHO)$")
                    list(APPEND _cdie_info_definitions "XXFC_STREAM_HAS_${_cdie_stream_name}=1")
                else()
                    list(APPEND _cdie_info_definitions "XXFC_STREAM_HAS_${_cdie_stream_name}=0")
                endif()
            else()
                list(APPEND _cdie_info_definitions "${_definition}")
            endif()
        endforeach()
        set_property(TARGET die_engine PROPERTY COMPILE_DEFINITIONS "${_cdie_info_definitions}")
    endif()

    # The current xxfclib format readers need these POSIX compatibility
    # definitions when compiled as strict C11. Keep them on the dependency
    # target, as CXFileUnpacker does, so cdie's C99 flags stay independent.
    if(UNIX)
        foreach(_CDIE_XXFC_TARGET xxfclib xxfclib_static xxfclib_shared die_engine)
            if(TARGET ${_CDIE_XXFC_TARGET})
                target_compile_options(${_CDIE_XXFC_TARGET} PRIVATE
                    "SHELL:-include string.h" "SHELL:-include stdlib.h")
                target_compile_definitions(${_CDIE_XXFC_TARGET} PRIVATE
                    _POSIX_C_SOURCE=200809L WEP_UNIX=AF_UNIX)
                if(APPLE)
                    target_compile_definitions(${_CDIE_XXFC_TARGET} PRIVATE _DARWIN_C_SOURCE)
                endif()
            endif()
        endforeach()
    endif()
endif()

if(COMMAND xxfclib_require_streams AND TARGET xxfclib::die_engine)
    xxfclib_require_streams(xxfclib::die_engine DATASTRUCTS)
endif()

# ------------------------------------------------------- language + warnings
#
# cdie's own targets only. See the xxfclib note above for why this block has to
# stay below the add_subdirectory() call.

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    add_compile_options(/W3)

    if(CDIE_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)

    if(CDIE_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# Resolve the database before the GUI so the macOS bundle can carry it.
set(CDIE_DATABASE_DIR "" CACHE PATH "Directory holding db, db_extra and db_custom")

set(CDIE_DATABASE_IMPLICIT OFF)

if(NOT CDIE_DATABASE_DIR)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/dep/Detect-It-Easy/db")
        get_filename_component(CDIE_DATABASE_DIR "${CMAKE_CURRENT_LIST_DIR}/dep/Detect-It-Easy" ABSOLUTE)
    elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../_mylibs/Detect-It-Easy/db")
        get_filename_component(CDIE_DATABASE_DIR "${CMAKE_CURRENT_LIST_DIR}/../_mylibs/Detect-It-Easy" ABSOLUTE)
        set(CDIE_DATABASE_IMPLICIT ON)
    endif()
endif()

file(GLOB CDIE_TRANSLATION_CATALOGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/translations/*.po")
add_subdirectory(src)

# --------------------------------------------------------------- library ----
#
# The die_library-compatible shared + static libraries (lib/). On by default;
# turn off to build only the console scanner.

option(CDIE_BUILD_LIBRARY "Build the die_library-compatible shared and static libraries" ON)

if(CDIE_BUILD_LIBRARY)
    add_subdirectory(lib)
endif()

# ------------------------------------------------------------------- tests --
#
# Public, generated-fixture integration tests. Private corpora remain optional.
option(CDIE_BUILD_TESTS "Build the public compatibility tests" OFF)

if(CDIE_BUILD_TESTS)
    if(NOT CDIE_BUILD_CONSOLE)
        message(FATAL_ERROR "CDIE_BUILD_TESTS requires CDIE_BUILD_CONSOLE=ON")
    endif()
    enable_testing()
    add_subdirectory(tests)
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/private_tests/CMakeLists.txt")
    add_subdirectory(private_tests)
endif()

# ---------------------------------------------------------------- documents

install(FILES
    "${CMAKE_CURRENT_LIST_DIR}/README.md"
    "${CMAKE_CURRENT_LIST_DIR}/LICENSE"
    "${CMAKE_CURRENT_LIST_DIR}/changelog.txt"
    DESTINATION . OPTIONAL)

install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/docs" DESTINATION . OPTIONAL)

# xxfclib is excluded from the install tree, so carry the notices for its
# statically linked decoder in the CDiE package itself.
if(XXFC_CDISASM_DIR AND EXISTS "${XXFC_CDISASM_DIR}/LICENSE")
    install(FILES
        "${XXFC_CDISASM_DIR}/LICENSE"
        "${XXFC_CDISASM_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "licenses/cdisasm" OPTIONAL)
    install(DIRECTORY "${XXFC_CDISASM_DIR}/LICENSES/"
        DESTINATION "licenses/cdisasm/LICENSES" OPTIONAL)
endif()

# ------------------------------------------------- signature database (opt)
#
# cdie is useless without a signature database, so a portable package bundles
# one when it can find it. Point CDIE_DATABASE_DIR at the directory holding
# db/, db_extra/ and db_custom/, or set it to NONE to package the binary
# alone.

# main_console.c resolve_database_path() looks for db/ next to the executable
# and nowhere else, so the database has to land in the same directory the
# console binary does. On Linux that is <prefix>/bin (src/console installs
# there); everywhere else both are flat at the prefix root.

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    set(CDIE_DATABASE_DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
    set(CDIE_DATABASE_DESTINATION ".")
endif()

if(CDIE_DATABASE_DIR AND (NOT CDIE_DATABASE_DIR STREQUAL "NONE") AND EXISTS "${CDIE_DATABASE_DIR}/db")
    message(STATUS "cdie: packaging the signature database from ${CDIE_DATABASE_DIR}")

    # The fallback points at an unversioned working copy, so two packages
    # built from the same cdie tag weeks apart can carry different signature
    # sets with nothing recording which. A packaging run should name the
    # directory (ideally a pinned checkout) explicitly.
    if(CDIE_DATABASE_IMPLICIT)
        message(WARNING "cdie: CDIE_DATABASE_DIR was not set; the database above was picked up implicitly "
                        "and its revision is not recorded. Pass -DCDIE_DATABASE_DIR=<path> for a package, "
                        "or -DCDIE_DATABASE_DIR=NONE to ship the binary alone.")
    endif()

    # The source is a live working copy, so editor and tooling directories sit
    # alongside the signatures; they must not be republished to end users.
    install(DIRECTORY "${CDIE_DATABASE_DIR}/db/" DESTINATION "${CDIE_DATABASE_DESTINATION}/db"
        PATTERN ".vscode" EXCLUDE
        PATTERN "_ai_provenance" EXCLUDE
        PATTERN ".git*" EXCLUDE)

    foreach(_extra db_extra db_custom)
        if(EXISTS "${CDIE_DATABASE_DIR}/${_extra}")
            install(DIRECTORY "${CDIE_DATABASE_DIR}/${_extra}/" DESTINATION "${CDIE_DATABASE_DESTINATION}/${_extra}"
                PATTERN ".vscode" EXCLUDE
                PATTERN "_ai_provenance" EXCLUDE
                PATTERN ".git*" EXCLUDE)
        endif()
    endforeach()
else()
    message(STATUS "cdie: no signature database packaged (set CDIE_DATABASE_DIR to bundle one)")
endif()

# -------------------------------------------------------------------- CPack

if(WIN32)
    set(CDIE_OSNAME "win")
elseif(APPLE)
    set(CDIE_OSNAME "mac")
else()
    set(CDIE_OSNAME "linux")
endif()

if(CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" CDIE_ARCH)

    # "Win32" is the MSVC spelling of x86; normalise it so the package name
    # does not read cdie_win_win32_...
    if(CDIE_ARCH STREQUAL "win32")
        set(CDIE_ARCH "x86")
    endif()
else()
    # Only Visual Studio generators set CMAKE_GENERATOR_PLATFORM, so on Ninja
    # and Makefiles the instruction set has to come from the processor name.
    # Pointer size alone cannot tell x86_64 from aarch64, and labelling an
    # arm64 package "x64" makes it collide with the real x64 one in release/.
    if(CMAKE_OSX_ARCHITECTURES)
        list(GET CMAKE_OSX_ARCHITECTURES 0 CDIE_PROCESSOR)
    else()
        set(CDIE_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    string(TOLOWER "${CDIE_PROCESSOR}" CDIE_PROCESSOR)

    if(CDIE_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(CDIE_ARCH "x64")
    elseif(CDIE_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(CDIE_ARCH "arm64")
    elseif(CDIE_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
        set(CDIE_ARCH "x86")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(CDIE_ARCH "x64")
    else()
        set(CDIE_ARCH "x86")
    endif()
endif()

set(CPACK_PACKAGE_NAME              "cdie")
set(CPACK_PACKAGE_VENDOR            "${X_COMPANYNAME}")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR     "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR     "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH     "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${X_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL      "${X_HOMEPAGE}")
set(CPACK_PACKAGE_CONTACT           "${X_MAINTAINER}")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_CURRENT_LIST_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "cdie")
set(CPACK_PACKAGE_FILE_NAME         "cdie_${CDIE_OSNAME}_${CDIE_ARCH}_portable_${PROJECT_VERSION}")
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)

include(CPack)

# GUI/TUI localization is independent of console and engine compatibility tests.
option(CDIE_BUILD_TRANSLATION_TESTS "Build interactive translation regressions" OFF)
if(CDIE_BUILD_TRANSLATION_TESTS)
    if(NOT TARGET cdie_ui_translation)
        message(FATAL_ERROR "Translation tests require CDIE_BUILD_GUI or CDIE_BUILD_TUI")
    endif()
    enable_testing()
    add_subdirectory(tests/translation)
endif()
find_program(CDIE_XGETTEXT_EXECUTABLE xgettext)
if(CDIE_XGETTEXT_EXECUTABLE AND TARGET cdie_ui_translation)
    file(GLOB_RECURSE _cdie_translation_inputs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/*.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tui/*.c")
    get_target_property(_cdie_widgets_source xxwidgets::xxwidgets SOURCE_DIR)
    file(GLOB_RECURSE _cdie_widget_inputs
        "${_cdie_widgets_source}/src/*.c" "${_cdie_widgets_source}/src/*.m")
    foreach(_source IN LISTS _cdie_widget_inputs)
        file(RELATIVE_PATH _relative "${CMAKE_CURRENT_SOURCE_DIR}" "${_source}")
        list(APPEND _cdie_translation_inputs "${_relative}")
    endforeach()
    add_custom_target(cdie_update_translations
        COMMAND "${CDIE_XGETTEXT_EXECUTABLE}" --from-code=UTF-8
            --keyword=CDIE_TR --keyword=CDIE_N_ --keyword=CDIE_FMT
            --keyword=cdie_ns_tr --keyword=cdie_ns_fmt
            --keyword=XXW_TR:2 --keyword=XXW_N_ --keyword=xxwidgets_app_gettext:2
            --keyword=xxwidgets_app_ngettext:2,3 --flag=CDIE_FMT:1:c-format --flag=cdie_translation_snprintf:3:c-format
            --flag=cdie_translation_swprintf:3:c-format --flag=CDIE_N_:1:pass-c-format
            --flag=cdie_ns_fmt:1:objc-format
            --sort-output --package-name=cdie --package-version=${CDIE_VERSION}
            --output=translations/cdie.pot ${_cdie_translation_inputs}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Extracting combined CDiE and xxwidgets translation catalog")
endif()
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(DIRECTORY translations/ DESTINATION "${CMAKE_INSTALL_DATADIR}/cdie/translations"
        FILES_MATCHING PATTERN "*.po" PATTERN "*.pot")
else()
    install(DIRECTORY translations/ DESTINATION translations
        FILES_MATCHING PATTERN "*.po" PATTERN "*.pot")
endif()
