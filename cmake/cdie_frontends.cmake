# The released desktop programs are die (GUI) and diec (console).
option(CDIE_BUILD_GUI "Build the die desktop application" ON)
option(CDIE_BUILD_CONSOLE "Build the diec command-line scanner" ON)

# Retain the old cache key so existing build directories migrate explicitly.
set(CDIE_BUILD_TUI OFF CACHE BOOL "The TUI application is no longer built" FORCE)
set(CDIE_BUILD_TUI OFF)
mark_as_advanced(CDIE_BUILD_TUI)

# macOS distributions contain only the native application bundle. Override
# stale cache values as well as parent-project options.
if(APPLE)
    set(CDIE_BUILD_CONSOLE OFF CACHE BOOL "Build the diec command-line scanner" FORCE)
    set(CDIE_BUILD_CONSOLE OFF)
    set(CDIE_BUILD_GUI ON CACHE BOOL "Build the die desktop application" FORCE)
    set(CDIE_BUILD_GUI ON)
endif()
