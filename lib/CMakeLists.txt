# The Detect It Easy library — die_library-compatible C API over the cdie
# engine. Builds both a shared and a static library from the same sources.
#
#   die_static  ->  die.lib      / libdie.a     (link the whole engine in)
#   die_shared  ->  die.dll      / libdie.so    (die.def-style C exports)
#
# The public header is lib/die.h; it is ABI-compatible with horsicq/die_library
# so the same samples build against it.

set(DIE_LIB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/die.h
    ${CMAKE_CURRENT_LIST_DIR}/die.c
    ${CDIE_LIB_SOURCES}
)

# ---------------------------------------------------------------- static ---

add_library(die_static STATIC ${DIE_LIB_SOURCES})
target_include_directories(die_static PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(die_static PUBLIC DIE_STATIC)

# On Windows the shared library's import lib is also die.lib, so the static
# archive takes a distinct name to avoid the collision. On Unix libdie.a and
# libdie.so coexist, so the plain name is kept.
if(WIN32)
    set_target_properties(die_static PROPERTIES OUTPUT_NAME die_static)
else()
    set_target_properties(die_static PROPERTIES OUTPUT_NAME die)
endif()

# ---------------------------------------------------------------- shared ---

add_library(die_shared SHARED ${DIE_LIB_SOURCES})
target_include_directories(die_shared PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(die_shared PRIVATE DIE_BUILD_SHARED)
set_target_properties(die_shared PROPERTIES OUTPUT_NAME die)

if(MSVC)
    # The dynamic CRT (/MD), matching how consumers are compiled by default, so
    # linking against these libraries does not warn about a runtime mismatch.
    set_property(TARGET die_static die_shared PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

if(UNIX)
    find_library(M_LIB m)

    if(M_LIB)
        target_link_libraries(die_static PRIVATE ${M_LIB})
        target_link_libraries(die_shared PRIVATE ${M_LIB})
    endif()
endif()

# ---------------------------------------------------------------- install --

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS die_shared die_static
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
else()
    install(TARGETS die_shared die_static
        RUNTIME DESTINATION "."
        LIBRARY DESTINATION "."
        ARCHIVE DESTINATION ".")
endif()

install(FILES ${CMAKE_CURRENT_LIST_DIR}/die.h DESTINATION "include" OPTIONAL)
