# Native xxwidgets front end. The scan engine remains in xxfclib; the widget
# library only needs its public type headers and does not link the engine.
if(APPLE)
    enable_language(OBJC)
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
    set_property(TARGET cdie_gui APPEND PROPERTY LINK_DEPENDS ${CDIE_TRANSLATION_CATALOGS})
    add_custom_command(TARGET cdie_gui POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DDATABASE_DIR=${CDIE_DATABASE_DIR}"
            "-DRESOURCES_DIR=$<TARGET_BUNDLE_CONTENT_DIR:cdie_gui>/Resources"
            "-DCDIE_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.."
            "-DCDISASM_DIR=${XXFC_CDISASM_DIR}"
            -P "${CMAKE_CURRENT_LIST_DIR}/bundle_resources.cmake"
        VERBATIM)
elseif(WIN32)
    add_executable(cdie_gui WIN32
        ${CDIE_LIB_SOURCES} gui_backend.c main_gui.c gui_entry.c cdie_gui.rc)
    cdie_stage_frontend_translations(cdie_gui)
    target_link_libraries(cdie_gui PRIVATE user32 gdi32 comdlg32 comctl32 shell32 ole32)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CDIE_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0>=3.16)
    add_executable(cdie_gui ${CDIE_LIB_SOURCES} gui_backend.c main_gui_linux.c)
    target_link_libraries(cdie_gui PRIVATE PkgConfig::CDIE_GTK3 m)
    cdie_stage_frontend_resources(cdie_gui)
endif()
target_sources(cdie_gui PRIVATE gui_png.c gui_tree.c gui_result_tree.c gui_scan_options.c gui_file_information.c gui_entropy.c gui_hash.c gui_strings.c gui_hex_view.c gui_disasm.c gui_memory_map.c gui_imports.c gui_exports.c gui_resources.c gui_symbols.c ../app/cdie_symbols.c ../app/cdie_resources.c ../app/cdie_exports.c ../app/cdie_imports.c ../app/cdie_memory_map.c ../app/cdie_disasm.c ../app/cdie_strings.c ../app/cdie_hash.c ../app/cdie_entropy.c ../app/cdie_file_info.c)
target_include_directories(cdie_gui PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(cdie_gui PRIVATE xxfclib::die_engine xxwidgets::xxwidgets cdie_ui_translation cdisasm::cdisasm)

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

if(CDIE_BUILD_TESTS AND UNIX AND NOT APPLE)
    get_target_property(_cdie_gui_test_sources cdie_gui SOURCES)
    list(REMOVE_ITEM _cdie_gui_test_sources main_gui_linux.c)
    add_executable(cdie_test_linux_settings ../../tests/gui_linux_settings.c ${_cdie_gui_test_sources})
    target_include_directories(cdie_test_linux_settings PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
    target_link_libraries(cdie_test_linux_settings PRIVATE xxfclib::die_engine xxwidgets::xxwidgets cdie_ui_translation cdisasm::cdisasm PkgConfig::CDIE_GTK3 m)
endif()
