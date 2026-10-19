# Native xxwidgets front end. The scan engine remains in xxfclib; the widget
# library only needs its public type headers and does not link the engine.
if(APPLE)
    enable_language(OBJC)
endif()

if(NOT TARGET xxwidgets::xxwidgets)
    set(_CDIE_DEFAULT_XXWIDGETS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../dep/xxwidgets")
    if(NOT EXISTS "${_CDIE_DEFAULT_XXWIDGETS_DIR}/CMakeLists.txt")
        set(_CDIE_DEFAULT_XXWIDGETS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../_mylibs/xxwidgets")
    endif()
    set(CDIE_XXWIDGETS_DIR "${_CDIE_DEFAULT_XXWIDGETS_DIR}"
        CACHE PATH "Location of the xxwidgets source tree")
    if(NOT EXISTS "${CDIE_XXWIDGETS_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "cdie_gui needs xxwidgets. Set CDIE_XXWIDGETS_DIR to its source tree.")
    endif()
    add_subdirectory("${CDIE_XXWIDGETS_DIR}" "${CMAKE_BINARY_DIR}/xxwidgets" EXCLUDE_FROM_ALL)
endif()

if(APPLE)
    add_executable(cdie_gui MACOSX_BUNDLE
        ${CDIE_LIB_SOURCES} gui_backend.c main_gui_macos.m)
    set_source_files_properties(main_gui_macos.m PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
    find_library(CDIE_APPKIT_FRAMEWORK AppKit REQUIRED)
    target_link_libraries(cdie_gui PRIVATE "${CDIE_APPKIT_FRAMEWORK}")
    set_target_properties(cdie_gui PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "cdie"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.ntinfo.cdie"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${CDIE_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${CDIE_VERSION}")
    add_custom_command(TARGET cdie_gui POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DDATABASE_DIR=${CDIE_DATABASE_DIR}"
            "-DRESOURCES_DIR=$<TARGET_BUNDLE_CONTENT_DIR:cdie_gui>/Resources"
            "-DCDIE_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.."
            "-DCDISASM_DIR=${XXFC_CDISASM_DIR}"
            -P "${CMAKE_CURRENT_LIST_DIR}/bundle_resources.cmake"
        VERBATIM)
else()
    add_executable(cdie_gui WIN32
        ${CDIE_LIB_SOURCES} gui_backend.c main_gui.c gui_entry.c cdie_gui.rc)
    target_link_libraries(cdie_gui PRIVATE user32 gdi32 comdlg32 comctl32 shell32 ole32)
endif()
target_sources(cdie_gui PRIVATE gui_tree.c gui_result_tree.c)
target_include_directories(cdie_gui PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(cdie_gui PRIVATE xxfclib::die_engine xxwidgets::xxwidgets)

if(MSVC)
    # The application resource owns the common-controls and DPI manifest.
    target_link_options(cdie_gui PRIVATE /MANIFEST:NO)
    target_compile_options(cdie_gui PRIVATE /utf-8)
    if(CDIE_NO_CRT AND NOT BUILD_SHARED_LIBS)
        # CDIE_NO_CRT continues to govern the console and engine archive.
        # xxwidgets uses the hosted runtime. Link it statically in this shape
        # to match the archive and avoid adding a runtime DLL to the package.
        set_property(TARGET cdie_gui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        if(TARGET xxwidgets)
            set_property(TARGET xxwidgets PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        endif()
        if(TARGET xxwidgets_scan_colors)
            set_property(TARGET xxwidgets_scan_colors PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        endif()
        message(STATUS "cdie_gui: xxwidgets frontend with static C runtime")
    endif()
endif()

install(TARGETS cdie_gui BUNDLE DESTINATION "." RUNTIME DESTINATION ".")
