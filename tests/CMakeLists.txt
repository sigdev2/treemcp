add_test(NAME bootstrap.runtime
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:treemcp>" -DSELF_CHECK=ON
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckCli.cmake")
add_test(NAME bootstrap.cli
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:treemcp>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckCli.cmake")
add_executable(check-broken-grammar ../src/main.cpp MisregisteredGrammar.cpp)
target_link_libraries(check-broken-grammar PRIVATE treemcp_core)
treemcp_set_warnings(check-broken-grammar)
add_test(NAME bootstrap.broken-grammar
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:check-broken-grammar>" -DBROKEN_GRAMMAR=ON
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckCli.cmake")
add_executable(check-decoder CheckDecoder.cpp)
target_link_libraries(check-decoder PRIVATE treemcp_core)
treemcp_set_warnings(check-decoder)
add_test(NAME protocol.decoder COMMAND check-decoder)

add_executable(check-protocol CheckProtocol.cpp CheckSearch.cpp)
target_link_libraries(check-protocol PRIVATE Qt6::Core)
treemcp_set_warnings(check-protocol)
add_test(NAME protocol.stdio COMMAND check-protocol "$<TARGET_FILE:treemcp>"
    "${PROJECT_SOURCE_DIR}/schemas")
add_test(NAME search.stdio COMMAND check-protocol "$<TARGET_FILE:treemcp>" --search)

add_executable(check-scheduler CheckScheduler.cpp)
target_link_libraries(check-scheduler PRIVATE treemcp_core)
treemcp_set_warnings(check-scheduler)
add_test(NAME scheduler.workers COMMAND check-scheduler)

set(catalog_files ${tool_descriptors})
list(TRANSFORM catalog_files PREPEND "${PROJECT_SOURCE_DIR}/")
add_executable(check-publication CheckPublication.cpp)
qt_add_resources(check-publication "publication_catalog" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}" FILES ${catalog_files})
target_link_libraries(check-publication PRIVATE treemcp_core)
treemcp_set_warnings(check-publication)
add_test(NAME scheduler.publication COMMAND check-publication)

add_executable(check-ignore-pattern CheckIgnorePattern.cpp)
target_link_libraries(check-ignore-pattern PRIVATE treemcp_core)
treemcp_set_warnings(check-ignore-pattern)
add_test(NAME workspace.ignore-pattern COMMAND check-ignore-pattern)

add_executable(check-entry-replacement CheckEntryReplacement.cpp)
target_link_libraries(check-entry-replacement PRIVATE treemcp_core)
treemcp_set_warnings(check-entry-replacement)
add_test(NAME workspace.entry-replacement COMMAND check-entry-replacement)

set(protocol_tests scheduler.workers bootstrap.runtime bootstrap.cli bootstrap.broken-grammar protocol.decoder protocol.stdio search.stdio
    workspace.ignore-pattern workspace.entry-replacement scheduler.publication)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME workspace.read-only COMMAND "${Python3_EXECUTABLE}"
        -B "${CMAKE_CURRENT_SOURCE_DIR}/check_read_only.py" "$<TARGET_FILE:treemcp>")
    list(APPEND protocol_tests workspace.read-only)
    add_test(NAME search.cli COMMAND "${Python3_EXECUTABLE}"
        -B "${CMAKE_CURRENT_SOURCE_DIR}/check_cli.py" "$<TARGET_FILE:treemcp>")
    add_test(NAME search.queries COMMAND "${Python3_EXECUTABLE}"
        -B "${CMAKE_CURRENT_SOURCE_DIR}/check_search_queries.py" "$<TARGET_FILE:treemcp>")
    add_test(NAME workspace.stdio COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/check_workspace.py" "$<TARGET_FILE:treemcp>")
    add_test(NAME workspace.ignore COMMAND "${Python3_EXECUTABLE}"
        -B "${CMAKE_CURRENT_SOURCE_DIR}/check_ignore.py" "$<TARGET_FILE:treemcp>")
    foreach(suite correctness corpus)
        add_test(NAME batch.${suite} COMMAND "${Python3_EXECUTABLE}"
            -B "${CMAKE_CURRENT_SOURCE_DIR}/check_batch.py" "$<TARGET_FILE:treemcp>" "${suite}")
    endforeach()
    add_test(NAME scheduler.stdio COMMAND "${Python3_EXECUTABLE}"
        -B "${CMAKE_CURRENT_SOURCE_DIR}/check_concurrency.py" "$<TARGET_FILE:treemcp>")
    add_test(NAME benchmark.mixed COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/check_benchmark.py")
    list(APPEND protocol_tests benchmark.mixed)
    if(TREEMCP_SEARCH_PROFILE)
        add_test(NAME benchmark.serialization COMMAND "${Python3_EXECUTABLE}"
            -B "${CMAKE_CURRENT_SOURCE_DIR}/check_benchmark.py" "$<TARGET_FILE:treemcp>")
        list(APPEND protocol_tests benchmark.serialization)
    endif()
    list(APPEND protocol_tests scheduler.stdio search.cli search.queries workspace.stdio workspace.ignore batch.correctness batch.corpus)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(check-batch-work CheckBatchWork.cpp)
    qt_add_resources(check-batch-work "batch_catalog" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
        FILES ${catalog_files})
    target_link_libraries(check-batch-work PRIVATE treemcp_core)
    target_link_options(check-batch-work PRIVATE "LINKER:--wrap=read"
        "LINKER:--wrap=ts_parser_parse_with_options" "LINKER:--wrap=ts_tree_delete")
    treemcp_set_warnings(check-batch-work)
    add_test(NAME batch.shared-work COMMAND check-batch-work)

    add_executable(check-parallel-search CheckParallelSearch.cpp)
    qt_add_resources(check-parallel-search "parallel_catalog" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}"
        FILES ${catalog_files})
    target_link_libraries(check-parallel-search PRIVATE treemcp_core)
    target_link_options(check-parallel-search PRIVATE
        "LINKER:--wrap=read" "LINKER:--wrap=readdir" "LINKER:--wrap=ts_tree_delete")
    treemcp_set_warnings(check-parallel-search)
    add_test(NAME search.parallel-order COMMAND check-parallel-search)
    set_tests_properties(search.parallel-order PROPERTIES TIMEOUT 30)

    add_executable(check-changed-read CheckChangedRead.cpp)
    target_link_libraries(check-changed-read PRIVATE treemcp_core)
    target_link_options(check-changed-read PRIVATE "LINKER:--wrap=read")
    treemcp_set_warnings(check-changed-read)
    add_test(NAME workspace.changed-read COMMAND check-changed-read)

    add_executable(check-traversal CheckTraversal.cpp)
    target_link_libraries(check-traversal PRIVATE treemcp_core)
    target_link_options(check-traversal PRIVATE "LINKER:--wrap=readdir" "LINKER:--wrap=fstat")
    treemcp_set_warnings(check-traversal)
    add_test(NAME workspace.traversal-limits COMMAND check-traversal)
    set_tests_properties(workspace.traversal-limits PROPERTIES TIMEOUT 30)
elseif(WIN32)
    add_executable(check-changed-read CheckWindowsChangedRead.cpp)
    target_link_libraries(check-changed-read PRIVATE treemcp_core)
    treemcp_set_warnings(check-changed-read)
    add_test(NAME workspace.changed-read COMMAND check-changed-read)
    list(APPEND protocol_tests workspace.changed-read)
endif()

if(TREEMCP_ENABLE_SANITIZERS)
    add_executable(check-request-cleanup CheckRequestCleanup.cpp)
    qt_add_resources(check-request-cleanup "cleanup_catalog" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}" FILES ${catalog_files})
    target_link_libraries(check-request-cleanup PRIVATE treemcp_core)
    treemcp_set_warnings(check-request-cleanup)
    add_test(NAME search.request-cleanup COMMAND check-request-cleanup)
    set_tests_properties(search.request-cleanup PROPERTIES TIMEOUT 30)
    add_executable(check-session-memory CheckSessionMemory.cpp)
    target_link_libraries(check-session-memory PRIVATE treemcp_core)
    treemcp_set_warnings(check-session-memory)
    add_test(NAME protocol.session-memory COMMAND check-session-memory)
endif()

set_tests_properties(${protocol_tests} PROPERTIES TIMEOUT 30)
if(Python3_Interpreter_FOUND)
    set_tests_properties(batch.corpus PROPERTIES TIMEOUT 600)
endif()
if(WIN32)
    get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_tests_properties(${protocol_tests} PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${compiler_directory}")
endif()
