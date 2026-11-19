add_test(NAME bootstrap.runtime COMMAND structural-mcp --self-check)
add_test(NAME bootstrap.cli
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:structural-mcp>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckCli.cmake")
add_executable(check-decoder CheckDecoder.cpp)
target_link_libraries(check-decoder PRIVATE treemcp_core)
treemcp_set_warnings(check-decoder)
add_test(NAME protocol.decoder COMMAND check-decoder)

add_executable(check-protocol CheckProtocol.cpp CheckSearch.cpp)
target_link_libraries(check-protocol PRIVATE Qt6::Core)
treemcp_set_warnings(check-protocol)
add_test(NAME protocol.stdio COMMAND check-protocol "$<TARGET_FILE:structural-mcp>"
    "${PROJECT_SOURCE_DIR}/schemas")
add_test(NAME search.stdio COMMAND check-protocol "$<TARGET_FILE:structural-mcp>" --search)

set(protocol_tests bootstrap.runtime bootstrap.cli protocol.decoder protocol.stdio search.stdio)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME workspace.stdio COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/check_workspace.py" "$<TARGET_FILE:structural-mcp>")
    list(APPEND protocol_tests workspace.stdio)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(check-changed-read CheckChangedRead.cpp)
    target_link_libraries(check-changed-read PRIVATE treemcp_core)
    target_link_options(check-changed-read PRIVATE "LINKER:--wrap=read")
    treemcp_set_warnings(check-changed-read)
    add_test(NAME workspace.changed-read COMMAND check-changed-read)
endif()

set_tests_properties(${protocol_tests} PROPERTIES TIMEOUT 30)
if(WIN32)
    get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_tests_properties(${protocol_tests} PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${compiler_directory}")
endif()
