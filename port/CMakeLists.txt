cmake_minimum_required(VERSION 3.20)
project(theoc_m1 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Homebrew (Apple Silicon) prefix for unicorn.
set(BREW /opt/homebrew)

add_executable(theoc
  src/main.cpp
  src/machine.cpp
  src/mvos.cpp
  src/traps.cpp
  src/video.cpp)

target_include_directories(theoc PRIVATE ${BREW}/include)
target_link_directories(theoc PRIVATE ${BREW}/lib)
target_link_libraries(theoc PRIVATE unicorn SDL2)
target_compile_options(theoc PRIVATE -Wall -Wextra -Wno-unused-parameter)

# NOTE: M1 runs entirely inside Unicorn's own guest address space, so the host
# does NOT yet need the low-4GB 1:1 mapping / shrunk __PAGEZERO. That linker
# flag (-Wl,-pagezero_size,0x1000) arrives with M2 zero-copy guest pointers.
