cmake_minimum_required(VERSION 3.20)
project(theoc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Build identity ----------------------------------------------------------
#
# Every run prints this in its first line, so a log names the build that
# produced it. That exists for one reason: a tester's report is only actionable
# if we know which binary they ran, and this port ships bundles that differ in
# ways a tester cannot see (the 2026-08-04 minimal-ffmpeg and WSAStartup changes
# both altered behaviour without altering anything visible in the UI).
#
# Resolved at CONFIGURE time, not build time. That means the string goes stale
# if you commit and rebuild without reconfiguring — accepted, because the two
# packaging scripts `rm -rf` their build directory and so always reconfigure,
# and those are the only builds that reach anyone else. A build-time custom
# command would be correct in general and would also re-link on every single
# build, which is a poor trade for a developer loop.
#
# -dirty is deliberate: a bundle built from an uncommitted tree says so, which
# is exactly the case where a bug report is otherwise unreproducible.
# THEOC_VERSION= overrides it for a tarball build with no git present.
set(THEOC_VERSION "" CACHE STRING "Build identity; defaults to git describe")
if(NOT THEOC_VERSION)
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE THEOC_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
  endif()
endif()
# Not an error: building from an exported tree with no git and no override is a
# legitimate thing to do. Say so in the banner rather than printing nothing,
# because a blank version reads as a bug in the stamping.
if(NOT THEOC_VERSION)
  set(THEOC_VERSION "unknown")
endif()
message(STATUS "theoc build identity: ${THEOC_VERSION}")

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

# THEOC_FFMPEG_PREFIX overrides *only* the libav* family, searched ahead of
# everything else. It exists for the packaged bundles: a stock ffmpeg carries
# every encoder and codec ever written and is ~90% of the bundle, while this port
# decodes exactly one thing (MPEG-1 video + MP2 audio out of an MPEG-PS file).
# tools/build-ffmpeg-min.sh builds that minimal ffmpeg; the package scripts point
# here at it. Unset — the normal case — nothing changes and the system ffmpeg is
# used, which is what every development build wants.
set(THEOC_FFMPEG_PREFIX "" CACHE PATH "Prefix of a minimal ffmpeg, searched before THEOC_PREFIX")
set(_theoc_av_inc_hint "${_theoc_inc_hint}")
set(_theoc_av_lib_hint "${_theoc_lib_hint}")
if(THEOC_FFMPEG_PREFIX)
  list(PREPEND _theoc_av_inc_hint "${THEOC_FFMPEG_PREFIX}/include")
  list(PREPEND _theoc_av_lib_hint "${THEOC_FFMPEG_PREFIX}/lib")
endif()

find_path(UNICORN_INCLUDE_DIR unicorn/unicorn.h        HINTS ${_theoc_inc_hint})
find_path(SDL2_INCLUDE_DIR    SDL2/SDL.h               HINTS ${_theoc_inc_hint})
find_path(LIBAV_INCLUDE_DIR   libavformat/avformat.h   HINTS ${_theoc_av_inc_hint} NO_DEFAULT_PATH)
find_path(LIBAV_INCLUDE_DIR   libavformat/avformat.h   HINTS ${_theoc_av_inc_hint})

find_library(UNICORN_LIBRARY     unicorn     HINTS ${_theoc_lib_hint})
find_library(SDL2_LIBRARY        SDL2        HINTS ${_theoc_lib_hint})
foreach(_av avformat avcodec avutil swscale swresample)
  string(TOUPPER "${_av}" _AV)
  # Two passes: the ffmpeg prefix wins outright when set (NO_DEFAULT_PATH stops
  # a system copy being picked up first), then the normal search as a fallback.
  find_library(${_AV}_LIBRARY ${_av} HINTS ${_theoc_av_lib_hint} NO_DEFAULT_PATH)
  find_library(${_AV}_LIBRARY ${_av} HINTS ${_theoc_av_lib_hint})
endforeach()

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
  src/cdaudio.cpp
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

# Only main.cpp is stamped, so editing the version does not rebuild the world.
set_source_files_properties(src/main.cpp PROPERTIES
  COMPILE_DEFINITIONS "THEOC_VERSION=\"${THEOC_VERSION}\"")

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
