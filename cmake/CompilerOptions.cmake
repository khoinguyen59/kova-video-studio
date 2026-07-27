set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /wd4702)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        # Keep optimized release binaries while retaining symbols for the private
        # release artifact. /DEBUG causes the linker to write a matching PDB.
        add_compile_options(/O2 /Zi)
        add_link_options(/DEBUG)
    else()
        add_compile_options(-O3)
    endif()
endif()
