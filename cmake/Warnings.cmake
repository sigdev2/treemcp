function(treemcp_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wformat=2
            -Wcast-qual
            -Wdouble-promotion
            -Wimplicit-fallthrough
            -Wmissing-declarations
            -Wnull-dereference
            -Wswitch-enum
            -Wundef
            -Wextra-semi
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Woverloaded-virtual
            -Wsuggest-override
            -Wzero-as-null-pointer-constant)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE
                -Wcast-align=strict
                -Wduplicated-branches
                -Wduplicated-cond
                -Wformat-overflow=2
                -Wformat-signedness
                -Wformat-truncation=2
                -Wlogical-op
                -Wuseless-cast)
        else()
            target_compile_options(${target} PRIVATE -Wcast-align)
        endif()
    endif()
endfunction()
