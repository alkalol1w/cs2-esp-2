cmake_minimum_required(VERSION 3.20)
project(cs2_esp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/W3 /O2 /EHsc)
    add_link_options(/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
else()
    add_compile_options(-O2 -Wall)
    add_link_options(-mwindows -e mainCRTStartup)
endif()

add_executable(cs2_esp
    main.cpp
    esp.hpp
    memory.hpp
    math.hpp
    offsets.hpp
    overlay.hpp
)

target_link_libraries(cs2_esp PRIVATE gdi32 user32)