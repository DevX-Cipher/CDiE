add_executable(cdie_test_schema_stream test_schema_stream.c)
target_link_libraries(cdie_test_schema_stream PRIVATE xxfclib::die_engine)
target_compile_features(cdie_test_schema_stream PRIVATE c_std_11)
set_target_properties(cdie_test_schema_stream PROPERTIES C_EXTENSIONS OFF)

if(UNIX)
    find_library(CDIE_SCHEMA_TEST_M_LIBRARY m)
    if(CDIE_SCHEMA_TEST_M_LIBRARY)
        target_link_libraries(cdie_test_schema_stream PRIVATE ${CDIE_SCHEMA_TEST_M_LIBRARY})
    endif()
endif()

if(MSVC AND CDIE_NO_CRT AND NOT BUILD_SHARED_LIBS)
    set_property(TARGET cdie_test_schema_stream PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

add_test(NAME cdie_schema_stream COMMAND cdie_test_schema_stream)
set_tests_properties(cdie_schema_stream PROPERTIES TIMEOUT 30)
