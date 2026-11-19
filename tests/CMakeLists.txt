add_test(NAME bootstrap.runtime COMMAND structural-mcp --self-check)
add_test(NAME bootstrap.cli
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:structural-mcp>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckCli.cmake")
set_tests_properties(bootstrap.runtime bootstrap.cli PROPERTIES TIMEOUT 15)
if(WIN32)
    get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_tests_properties(bootstrap.runtime bootstrap.cli PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${compiler_directory}")
endif()
