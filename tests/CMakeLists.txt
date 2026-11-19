add_test(NAME bootstrap.runtime COMMAND structural-mcp --self-check)
add_test(NAME bootstrap.cli
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:structural-mcp>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckCli.cmake")
add_executable(check-decoder CheckDecoder.cpp)
target_link_libraries(check-decoder PRIVATE treemcp_core)
treemcp_set_warnings(check-decoder)
add_test(NAME protocol.decoder COMMAND check-decoder)

set(protocol_tests bootstrap.runtime bootstrap.cli protocol.decoder)
set_tests_properties(${protocol_tests} PROPERTIES TIMEOUT 15)
if(WIN32)
    get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_tests_properties(${protocol_tests} PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${compiler_directory}")
endif()
