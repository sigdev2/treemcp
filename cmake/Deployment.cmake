option(TREEMCP_BUNDLE_RUNTIME "Install shared runtime dependencies alongside the executable" ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(treemcp PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    # An inherited RPATH also resolves dependencies of copied third-party libraries.
    target_link_options(treemcp PRIVATE "LINKER:--disable-new-dtags")
endif()

install(TARGETS treemcp RUNTIME_DEPENDENCY_SET treemcp_runtime
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(TREEMCP_BUNDLE_RUNTIME)
    get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(WIN32)
        install(RUNTIME_DEPENDENCY_SET treemcp_runtime
            DIRECTORIES "$<TARGET_FILE_DIR:Qt6::Core>" "${compiler_directory}"
            PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
            POST_EXCLUDE_REGEXES ".*[/\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\].*"
                ".*[/\\][Ww][Ii][Nn][Ss][Xx][Ss][/\\].*"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        install(RUNTIME_DEPENDENCY_SET treemcp_runtime
            DIRECTORIES "$<TARGET_FILE_DIR:Qt6::Core>"
            PRE_EXCLUDE_REGEXES "^ld-linux.*" "^ld-musl.*"
                "^lib(c|m|pthread|dl|rt|resolv|util|anl|nss_.*)\\.so(\\..*)?$"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    else()
        message(FATAL_ERROR "Runtime bundling is supported on Windows and Linux only.")
    endif()
endif()
