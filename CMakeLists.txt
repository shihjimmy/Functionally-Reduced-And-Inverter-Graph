cmake_minimum_required(VERSION 3.16)
project(fraig VERSION 1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language level and flags
#
# C++17, -O3.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# Release is "-O3" (asserts stay enabled).
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

add_compile_definitions(TA_KB_SETTING)

# ---------------------------------------------------------------------------
# Options
#   FRAIG_WERROR   turn warnings of our own code into errors (CI uses this)
#   FRAIG_SANITIZE build with AddressSanitizer + UndefinedBehaviorSanitizer
# ---------------------------------------------------------------------------
option(FRAIG_WERROR   "Treat warnings in our own code as errors" OFF)
option(FRAIG_SANITIZE "Build with AddressSanitizer and UBSan"    OFF)

# Warning flags are attached to OUR targets only (util, cmd, cir, fraig).
# src/sat is MiniSat, third-party code that we do not modify, so it is built
# without warnings and its headers are included as "system" headers.
add_library(fraig_warnings INTERFACE)
target_compile_options(fraig_warnings INTERFACE -Wall -Wextra)
if(FRAIG_WERROR)
  target_compile_options(fraig_warnings INTERFACE -Werror)
endif()

if(FRAIG_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
  # Sanitized runs want debug info and a light optimization level.
  set(CMAKE_CXX_FLAGS_RELEASE "-O1 -g")
endif()

# ---------------------------------------------------------------------------
# Libraries -- one per directory under src/.
# Every library exports its own directory as an include path, so a header is
# found by its plain name ("cmdParser.h") and the old include/ directory of
# symbolic links is no longer needed.
# ---------------------------------------------------------------------------
add_library(util STATIC
  src/util/myGetChar.cpp
  src/util/myString.cpp
  src/util/util.cpp)
target_include_directories(util PUBLIC src/util)

add_library(sat STATIC
  src/sat/File.cpp
  src/sat/Proof.cpp
  src/sat/Solver.cpp)
target_include_directories(sat SYSTEM PUBLIC src/sat)
target_compile_options(sat PRIVATE -w)

add_library(cmd STATIC
  src/cmd/cmdCharDef.cpp
  src/cmd/cmdCommon.cpp
  src/cmd/cmdParser.cpp
  src/cmd/cmdReader.cpp)
target_include_directories(cmd PUBLIC src/cmd)
target_link_libraries(cmd PUBLIC util)

add_library(cir STATIC
  src/cir/cirCheck.cpp
  src/cir/cirCmd.cpp
  src/cir/cirFraig.cpp
  src/cir/cirGate.cpp
  src/cir/cirJson.cpp
  src/cir/cirMgr.cpp
  src/cir/cirOpt.cpp
  src/cir/cirSim.cpp
  src/cir/cirVerify.cpp)
target_include_directories(cir PUBLIC src/cir)
target_link_libraries(cir PUBLIC cmd sat util)

# ---------------------------------------------------------------------------
# The executable.  It is written to <build dir>/fraig.
# ---------------------------------------------------------------------------
add_executable(fraig
  src/main/main.cpp
  src/main/guiServer.cpp)
# The GUI runs long commands on a worker thread.
find_package(Threads REQUIRED)
target_link_libraries(fraig PRIVATE cir cmd sat util Threads::Threads)

foreach(t util cmd cir fraig)
  target_link_libraries(${t} PRIVATE fraig_warnings)
endforeach()

# ---------------------------------------------------------------------------
# Tests (run with:  ctest --test-dir build --output-on-failure)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
