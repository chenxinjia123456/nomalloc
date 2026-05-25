set(NOMALLOC_COMPILE_OPTIONS "")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND NOMALLOC_COMPILE_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
        -fno-strict-aliasing
        -fno-omit-frame-pointer
    )
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND NOMALLOC_COMPILE_OPTIONS
            -O3
            -DNDEBUG
        )
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND NOMALLOC_COMPILE_OPTIONS
            -O0
            -g3
        )
        if(NOMALLOC_ENABLE_SANITIZERS)
            list(APPEND NOMALLOC_COMPILE_OPTIONS
                -fsanitize=address
                -fsanitize=undefined
            )
        endif()
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        list(APPEND NOMALLOC_COMPILE_OPTIONS
            -O2
            -g
            -DNDEBUG
        )
    elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        list(APPEND NOMALLOC_COMPILE_OPTIONS
            -Os
            -DNDEBUG
        )
    endif()
endif()

if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
    list(APPEND NOMALLOC_COMPILE_OPTIONS
        /W4
        /MP
    )
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND NOMALLOC_COMPILE_OPTIONS
            /O2
            /DNDEBUG
        )
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND NOMALLOC_COMPILE_OPTIONS
            /Od
            /Zi
        )
    endif()
endif()