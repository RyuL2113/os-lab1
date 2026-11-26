cmake_minimum_required(VERSION 3.16)
project(Lab3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(proclaunch STATIC src/proclaunch.c)
target_include_directories(proclaunch PUBLIC include)

add_executable(lab3 src/main.cpp src/platform.cpp src/shared.cpp)
target_link_libraries(lab3 PRIVATE proclaunch)

if(WIN32)
    if(MINGW)
        # чтобы lab3.exe запускался и вне терминала MSYS2
        target_link_options(lab3 PRIVATE -static)
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(lab3 PRIVATE Threads::Threads)
    # shm_open в старых glibc лежит в librt
    find_library(RT_LIB rt)
    if(RT_LIB)
        target_link_libraries(lab3 PRIVATE ${RT_LIB})
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lab3 PRIVATE -Wall -Wextra)
endif()
