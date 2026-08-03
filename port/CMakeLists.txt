cmake_minimum_required(VERSION 3.20)
project(theoc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dependency discovery ----------------------------------------------------
#
# Plain find_path/find_library rather than pkg-config. The .pc files ship with
# the Homebrew formulae, but the pkg-config *binary* frequently is not installed
# on a Mac, so `find_package(PkgConfig REQUIRED)` would break the macOS build
# that already works. These libraries are simple enough not to need it.
#
# Every third-party include in port/src is prefixed — <SDL2/SDL.h>,
# <unicorn/unicorn.h>, <libavformat/avformat.h> — so all that is needed is the
# parent include directory, which is already the default on Debian/Ubuntu and
# needs the Homebrew prefix on macOS.
#
# THEOC_PREFIX adds one search root. It defaults to the Homebrew prefix on
# macOS and to nothing elsewhere; override it for an unusual install:
#   cmake -S port -B port/build -DTHEOC_PREFIX=/usr/local
if(APPLE)
  set(THEOC_PREFIX "/opt/homebrew" CACHE PATH "Extra search prefix for dependencies")
else()
  set(THEOC_PREFIX "" CACHE PATH "Extra search prefix for dependencies")
endif()

set(_theoc_inc_hint "")
set(_theoc_lib_hint "")
if(THEOC_PREFIX)
  set(_theoc_inc_hint "${THEOC_PREFIX}/include")
  set(_theoc_lib_hint "${THEOC_PREFIX}/lib")
endif()

find_path(UNICORN_INCLUDE_DIR unicorn/unicorn.h        HINTS ${_theoc_inc_hint})
find_path(SDL2_INCLUDE_DIR    SDL2/SDL.h               HINTS ${_theoc_inc_hint})
find_path(LIBAV_INCLUDE_DIR   libavformat/avformat.h   HINTS ${_theoc_inc_hint})

find_library(UNICORN_LIBRARY     unicorn     HINTS ${_theoc_lib_hint})
find_library(SDL2_LIBRARY        SDL2        HINTS ${_theoc_lib_hint})
find_library(AVFORMAT_LIBRARY    avformat    HINTS ${_theoc_lib_hint})
find_library(AVCODEC_LIBRARY     avcodec     HINTS ${_theoc_lib_hint})
find_library(AVUTIL_LIBRARY      avutil      HINTS ${_theoc_lib_hint})
find_library(SWSCALE_LIBRARY     swscale     HINTS ${_theoc_lib_hint})
find_library(SWRESAMPLE_LIBRARY  swresample  HINTS ${_theoc_lib_hint})

# Report *everything* missing at once. A one-at-a-time failure on a fresh box
# turns "install the dependencies" into five separate rounds of cmake.
set(_theoc_missing "")
foreach(_v
    UNICORN_INCLUDE_DIR SDL2_INCLUDE_DIR LIBAV_INCLUDE_DIR
    UNICORN_LIBRARY SDL2_LIBRARY AVFORMAT_LIBRARY AVCODEC_LIBRARY
    AVUTIL_LIBRARY SWSCALE_LIBRARY SWRESAMPLE_LIBRARY)
  if(NOT ${_v})
    list(APPEND _theoc_missing ${_v})
  endif()
endforeach()
if(_theoc_missing)
  message(FATAL_ERROR
    "Missing dependencies: ${_theoc_missing}\n"
    "  macOS:         brew install unicorn sdl2 ffmpeg\n"
    "  Debian/Ubuntu: apt install libunicorn-dev libsdl2-dev libavformat-dev "
    "libavcodec-dev libavutil-dev libswscale-dev libswresample-dev\n"
    "  Unicorn must be version 2.x. If the distro still ships 1.x, build it from\n"
    "  source and point cmake at it with -DTHEOC_PREFIX=<prefix>.")
endif()

# The watchdog runs on a std::thread, which needs -pthread on Linux and nothing
# on macOS. Without this the Linux link fails at std::thread's constructor.
find_package(Threads REQUIRED)

# ---- Target ------------------------------------------------------------------
add_executable(theoc
  src/main.cpp
  src/machine.cpp
  src/guestlink.cpp
  src/traps.cpp
  src/video.cpp
  src/mpeg.cpp
  src/blit.cpp
  # Legacy pure-HLE MVOS method layer (superseded by guest libmvos):
  # src/mvos.cpp
)

target_include_directories(theoc PRIVATE
  ${UNICORN_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${LIBAV_INCLUDE_DIR})

target_link_libraries(theoc PRIVATE
  ${UNICORN_LIBRARY}
  ${SDL2_LIBRARY}
  ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY}
  ${SWSCALE_LIBRARY} ${SWRESAMPLE_LIBRARY}
  Threads::Threads)

target_compile_options(theoc PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---- Windows host ------------------------------------------------------------
# Two things, both of which fail confusingly if missed:
#
#   ws2_32   Winsock2. Without it every socket symbol is an undefined
#            __imp_* reference at link time.
#   winmm    timeBeginPeriod/timeGetDevCaps — the scheduler-granularity controls
#            the sleep model depends on (see tools/win_timing_probe.cpp).
#
# SDL_MAIN_HANDLED is the subtle one. On Windows, including SDL.h #defines main
# to SDL_main and expects you to link SDL2main, which supplies a WinMain that
# calls it. This host wants its own main() as the real entry point, so tell SDL
# to keep its hands off rather than adding SDL2main and the mingw32 shim.
if(WIN32)
  target_link_libraries(theoc PRIVATE ws2_32 winmm)
  target_compile_definitions(theoc PRIVATE SDL_MAIN_HANDLED)
endif()

# NOTE: the host runs entirely inside Unicorn's own guest address space, so it
# does NOT need the low-4GB 1:1 mapping / shrunk __PAGEZERO. That macOS linker
# flag (-Wl,-pagezero_size,0x1000) would only arrive with zero-copy guest
# pointers, which the guest-libmvos architecture never needed.
