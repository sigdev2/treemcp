cmake_minimum_required(VERSION 3.22)

project(treemcp VERSION 0.1.0 LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "Use a separate build directory, for example cmake --preset linux-debug.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(GNUInstallDirs)
include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

# Keep checkout paths out of diagnostics and debug metadata in project-built objects.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options("-ffile-prefix-map=${PROJECT_SOURCE_DIR}=."
        "-ffile-prefix-map=${PROJECT_BINARY_DIR}=./build")
endif()

include(cmake/Dependencies.cmake)
find_package(Threads REQUIRED)

configure_file(cmake/Version.hpp.in generated/treemcp/Version.hpp @ONLY)

add_library(treemcp_core STATIC
    src/app/SelfCheck.cpp
    src/languages/LanguageRegistry.cpp
    src/mcp/Framer.cpp
    src/mcp/JsonRpc.cpp
    src/scheduler/Scheduler.cpp
    src/mcp/Session.cpp
    src/mcp/StdioServer.cpp
    src/mcp/Tools.cpp
    src/search/BatchSearch.cpp
    src/search/Matches.cpp
    src/search/Query.cpp
    src/search/SingleFile.cpp
    src/workspace/Workspace.cpp
    src/workspace/WorkspaceFilter.cpp
    src/workspace/WorkspaceScan.cpp
    src/workspace/WorkspaceNode.cpp
)
if(WIN32)
    target_sources(treemcp_core PRIVATE src/workspace/WorkspaceWindows.cpp)
    target_link_libraries(treemcp_core PRIVATE ntdll)
else()
    target_sources(treemcp_core PRIVATE src/workspace/WorkspacePosix.cpp)
endif()
target_include_directories(treemcp_core PUBLIC src/app src/languages src/mcp src/search src/scheduler src/workspace "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(treemcp_core PUBLIC Qt6::Core tree_sitter tree_sitter_cpp tree_sitter_python)
target_link_libraries(treemcp_core PRIVATE yyjson Threads::Threads)
treemcp_set_warnings(treemcp_core)

add_executable(structural-mcp src/main.cpp)
set(tool_descriptors schemas/languages.json schemas/query_validate.json schemas/syntax_tree.json schemas/structural_search.json schemas/batch_search.json)
qt_add_resources(structural-mcp "tool_catalog" PREFIX "/" FILES ${tool_descriptors})
target_link_libraries(structural-mcp PRIVATE treemcp_core)
treemcp_set_warnings(structural-mcp)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(cmake/Deployment.cmake)
install(FILES README.md ROADMAP.md LICENSE StyleGuide.CPP.md .clang-format
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs")
install(FILES ${tool_descriptors} DESTINATION "${CMAKE_INSTALL_DOCDIR}/schemas")
install(DIRECTORY config/ DESTINATION "${CMAKE_INSTALL_DOCDIR}/config")
install(FILES third_party/dependencies.lock.json third_party/SHA256SUMS
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/third_party")
foreach(dependency tree-sitter tree-sitter-cpp tree-sitter-python yyjson)
    install(FILES "third_party/${dependency}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/third_party/${dependency}")
endforeach()
install(FILES third_party/tree-sitter/lib/src/unicode/LICENSE
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/third_party/tree-sitter/unicode")

set(CPACK_PACKAGE_NAME treemcp)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
set(CPACK_STRIP_FILES ON)
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
# CPack's source directory walk does not honor Git ignores.
configure_file(cmake/RejectSourcePackage.cmake "${CPACK_SOURCE_OUTPUT_CONFIG_FILE}" COPYONLY)
