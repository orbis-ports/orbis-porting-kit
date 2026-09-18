# The platform services: the parts of a port that are about the CONSOLE rather than about the
# engine, taken out of the ports that first wrote them.
#
#   add_subdirectory(<kit>/services orbis-services)
#   target_link_libraries(mygame PRIVATE orbis::services)
#
# Or on its own, which is what CI does, to prove they compile without an engine around them:
#
#   cmake -S services -B build -DCMAKE_TOOLCHAIN_FILE=<kit>/cmake/ps4-openorbis.cmake
#
# ⚠ WHAT IS DELIBERATELY NOT HERE. The files these came from are 2200 lines between them and only
# about 780 arrived, because the rest was engine: a Tempest SoundDevice backend, a software mixer, a
# resampler, an IMA-ADPCM decoder, one game's save-dialog wiring and four probes of that game's
# archives. Carrying those would have made this a kit for one engine, which is the failure it exists
# to prevent - it would look like reuse and be a second copy.
cmake_minimum_required(VERSION 3.16)

# Same ordering rule as the examples: CMake reads CMAKE_TOOLCHAIN_FILE when project() runs, so
# nothing it sets exists before this line and a guard above it can only ever fire.
project(orbis_services CXX)

if(NOT PS4)
  message(FATAL_ERROR
    "configure with -DCMAKE_TOOLCHAIN_FILE=<kit>/cmake/ps4-openorbis.cmake. These are console "
    "services; there is nothing here that builds for a host.")
endif()

add_library(orbis-services STATIC
  audio/orbis_audio.cpp
  ime/orbis_ime.cpp
  data/orbis_data.cpp)

target_include_directories(orbis-services PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}/audio"
  "${CMAKE_CURRENT_LIST_DIR}/ime"
  "${CMAKE_CURRENT_LIST_DIR}/data")

# C++17 for the fold-free bits actually used; the SDK's libc++ is what provides it.
target_compile_features(orbis-services PUBLIC cxx_std_17)

# ⚠ THE SDK LIBRARIES ARE NAMED HERE, NOT LEFT TO THE CONSUMER. Each service is useless without
# its stub library, and a missing one is an undefined symbol at the final link of a title - far from
# the file that needed it. PUBLIC so they reach the executable's link line through this target.
target_link_libraries(orbis-services PUBLIC
  -lSceAudioOut -lSceUserService -lSceIme -lSceCommonDialog -lSceSysmodule)

add_library(orbis::services ALIAS orbis-services)
