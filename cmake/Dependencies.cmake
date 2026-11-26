find_package(Qt6 6.4.1 REQUIRED COMPONENTS Core)

# Verify the exact vendored bytes before compiling; configuration never downloads code.
include("${CMAKE_CURRENT_LIST_DIR}/VerifyVendor.cmake")

add_library(yyjson STATIC "${PROJECT_SOURCE_DIR}/third_party/yyjson/src/yyjson.c")
target_include_directories(yyjson SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/yyjson/src")
target_compile_definitions(yyjson PUBLIC YYJSON_DISABLE_WRITER YYJSON_DISABLE_NON_STANDARD)

add_library(tree_sitter STATIC "${PROJECT_SOURCE_DIR}/third_party/tree-sitter/lib/src/lib.c")
target_include_directories(tree_sitter SYSTEM PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/tree-sitter/lib/include")
target_include_directories(tree_sitter PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/tree-sitter/lib/src")
if(UNIX)
    target_compile_definitions(tree_sitter PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
endif()

set(grammar_targets)
foreach(grammar cpp python c rust javascript typescript tsx go java)
    if(grammar STREQUAL "typescript" OR grammar STREQUAL "tsx")
        set(grammar_dir "${PROJECT_SOURCE_DIR}/third_party/tree-sitter-typescript/${grammar}/src")
    else()
        set(grammar_dir "${PROJECT_SOURCE_DIR}/third_party/tree-sitter-${grammar}/src")
    endif()
    add_library(tree_sitter_${grammar} STATIC "${grammar_dir}/parser.c")
    if(EXISTS "${grammar_dir}/scanner.c")
        target_sources(tree_sitter_${grammar} PRIVATE "${grammar_dir}/scanner.c")
    endif()
    list(APPEND grammar_targets tree_sitter_${grammar})
    target_include_directories(tree_sitter_${grammar} SYSTEM PRIVATE "${grammar_dir}")
endforeach()
