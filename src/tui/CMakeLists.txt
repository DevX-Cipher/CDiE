add_executable(cdie_tui
    ${CDIE_LIB_SOURCES}
    main_tui.c
    ../gui/gui_backend.c
    ../gui/gui_tree.c
    ../gui/gui_scan_options.c
    ../gui/gui_result_tree.c)
target_include_directories(cdie_tui PRIVATE "${CMAKE_CURRENT_LIST_DIR}/.." "${CMAKE_CURRENT_LIST_DIR}/../gui")
target_link_libraries(cdie_tui PRIVATE xxfclib::die_engine xxwidgets::xxwidgets cdie_ui_translation)
if(MSVC)
    target_compile_options(cdie_tui PRIVATE /utf-8)
    if(CDIE_NO_CRT AND NOT BUILD_SHARED_LIBS)
        set_property(TARGET cdie_tui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
elseif(MINGW)
    target_link_options(cdie_tui PRIVATE -municode)
endif()

if(UNIX)
    find_library(CDIE_TUI_MATH_LIBRARY m)
    if(CDIE_TUI_MATH_LIBRARY)
        target_link_libraries(cdie_tui PRIVATE ${CDIE_TUI_MATH_LIBRARY})
    endif()
endif()
cdie_stage_frontend_resources(cdie_tui)

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS cdie_tui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
    install(TARGETS cdie_tui RUNTIME DESTINATION ".")
endif()
