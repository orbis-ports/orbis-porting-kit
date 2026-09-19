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
# about 667 arrived, because the rest was engine: a Tempest SoundDevice backend, a software mixer, a
# resampler, an IMA-ADPCM decoder, one game's save-dialog wiring and four probes of that game's
# archives. Carrying those would have made this a kit for one engine, which is the failure it exists
# to prevent - it would look like reuse and be a second copy. The audio pump went the same way on
# 2026-09-19, one step later and for the same reason: see the note above add_library().
cmake_minimum_required(VERSION 3.16)

# Same ordering rule as the examples: CMake reads CMAKE_TOOLCHAIN_FILE when project() runs, so
# nothing it sets exists before this line and a guard above it can only ever fire.
project(orbis_services CXX)

if(NOT PS4)
  message(FATAL_ERROR
    "configure with -DCMAKE_TOOLCHAIN_FILE=<kit>/cmake/ps4-openorbis.cmake. These are console "
    "services; there is nothing here that builds for a host.")
endif()

# ⚠ audio/ IS HEADER-ONLY ON PURPOSE, AND IS NOT A SERVICE. orbis_audio.cpp was deleted on
# 2026-09-19: it owned a std::thread and the console's single output port, which a second program in
# the same process would notice, so by the kit's boundary rule it is a BACKEND and belongs to
# whoever owns that thread - SDL2's src/audio/orbis driver, or the engine's own sound device.
# services/audio/orbis_audio.h stays as the record of what the hardware measured, including the
# unresolved disagreement about what Close does to the port. It declares no functions, so there is
# nothing here to compile; it is on the include path below so the constants are reachable.
add_library(orbis-services STATIC
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
# -lSceAudioOut went with orbis_audio.cpp on 2026-09-19; nothing here calls sceAudioOut* any more.
# libSceUserService stays: orbis_ime.cpp calls sceUserServiceInitialize and
# sceUserServiceGetInitialUser (grep).
target_link_libraries(orbis-services PUBLIC
  -lSceUserService -lSceIme -lSceCommonDialog -lSceSysmodule)

add_library(orbis::services ALIAS orbis-services)
