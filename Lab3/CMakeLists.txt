cmake_minimum_required(VERSION 3.10)
project(cross_platform_counter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(counter_app
    src/main.cpp
    src/ipc.cpp
    src/process.cpp
)

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(counter_app PRIVATE Threads::Threads)
endif()

if(MSVC)
    target_compile_options(counter_app PRIVATE /W4)
else()
    target_compile_options(counter_app PRIVATE -Wall -Wextra -Wpedantic)
endif()