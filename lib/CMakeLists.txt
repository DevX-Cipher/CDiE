# The Detect It Easy library — die_library-compatible C API over the cdie
# engine. Builds both a shared and a static library from the same sources.
#
#   die_static  ->  die_static.lib / libdie.a     (CRT-free)
#   die_shared  ->  die.dll        / libdie.so    (default CRT)
#
# The public header is lib/die.h; it is ABI-compatible with horsicq/die_library.

option(DIE_BUILD_STATIC "Build the static library" ON)
option(DIE_BUILD_SHARED "Build the shared library" ON)

set(DIE_LIB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/die.h
    ${CMAKE_CURRENT_LIST_DIR}/die.c
    ${CDIE_LIB_SOURCES}
)

# ---------------------------------------------------------------- static ---

if(DIE_BUILD_STATIC)
    add_library(die_static STATIC ${DIE_LIB_SOURCES})
    target_include_directories(die_static PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(die_static PUBLIC DIE_STATIC)

    if(WIN32)
        set_target_properties(die_static PROPERTIES OUTPUT_NAME die_static)
    else()
        set_target_properties(die_static PROPERTIES OUTPUT_NAME die)
    endif()
endif()

# ---------------------------------------------------------------- shared ---

if(DIE_BUILD_SHARED)
    add_library(die_shared SHARED ${DIE_LIB_SOURCES})
    target_include_directories(die_shared PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(die_shared PRIVATE DIE_BUILD_SHARED)
    set_target_properties(die_shared PROPERTIES OUTPUT_NAME die)
endif()

# ---------------------------------------------------------------- MSVC CRT‑free flags (STATIC ONLY)

if (MSVC)
    if(DIE_BUILD_STATIC)
        target_compile_options(die_static PRIVATE
            /GS-        
            /Zl        
            /kernel     
            /GR-        
            /EHsc-      
            /DNDEBUG    
        )
        target_link_options(die_static PRIVATE
            /NODEFAULTLIB   
        )
    endif()
endif()

# ---------------------------------------------------------------- install --

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)

    if(DIE_BUILD_STATIC)
        install(TARGETS die_static
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()

    if(DIE_BUILD_SHARED)
        install(TARGETS die_shared
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()

else()
    if(DIE_BUILD_STATIC)
        install(TARGETS die_static
            RUNTIME DESTINATION "."
            LIBRARY DESTINATION "."
            ARCHIVE DESTINATION ".")
    endif()

    if(DIE_BUILD_SHARED)
        install(TARGETS die_shared
            RUNTIME DESTINATION "."
            LIBRARY DESTINATION "."
            ARCHIVE DESTINATION ".")
    endif()
endif()

install(FILES ${CMAKE_CURRENT_LIST_DIR}/die.h DESTINATION "include" OPTIONAL)
