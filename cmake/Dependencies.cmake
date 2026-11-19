find_package(Qt6 6.4.1 REQUIRED COMPONENTS Core)

# Verify the exact vendored bytes before compiling; configuration never downloads code.
include("${CMAKE_CURRENT_LIST_DIR}/VerifyVendor.cmake")

add_library(tree_sitter STATIC "${PROJECT_SOURCE_DIR}/third_party/tree-sitter/lib/src/lib.c")
target_include_directories(tree_sitter SYSTEM PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/tree-sitter/lib/include")
target_include_directories(tree_sitter PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/tree-sitter/lib/src")
if(UNIX)
    target_compile_definitions(tree_sitter PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
endif()

foreach(grammar cpp python)
    set(grammar_dir "${PROJECT_SOURCE_DIR}/third_party/tree-sitter-${grammar}/src")
    add_library(tree_sitter_${grammar} STATIC "${grammar_dir}/parser.c" "${grammar_dir}/scanner.c")
    target_include_directories(tree_sitter_${grammar} SYSTEM PRIVATE "${grammar_dir}")
endforeach()
