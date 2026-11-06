# SPDX-License-Identifier: CC0-1.0
if(NOT PS5 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "PE2_PS5_HOST requires the x86-64 public PS5 toolchain")
endif()
find_package(SDL3 CONFIG REQUIRED)
# FreeBSD SDL exports a separate libm dependency. This native application
# supplies those symbols through its reviewed libc runtime response file.
if(TARGET SDL3::SDL3-static)
    get_target_property(_pe2_sdl_links SDL3::SDL3-static INTERFACE_LINK_LIBRARIES)
    list(REMOVE_ITEM _pe2_sdl_links m "$<LINK_ONLY:m>")
    set_property(TARGET SDL3::SDL3-static PROPERTY INTERFACE_LINK_LIBRARIES "${_pe2_sdl_links}")
endif()
# The retained native runtime supplies math through libc/libc_stub_weak, not
# a separate libm. Keep WebP decode synchronous; SDL still owns game threads.
# A final SDK link must resolve every referenced math/thread symbol normally.
set(HAVE_MATH_LIBRARY TRUE CACHE BOOL "Native runtime supplies math" FORCE)
set(WEBP_USE_THREAD OFF CACHE BOOL "Synchronous native WebP decode" FORCE)
set(PE2_PS5_LINK_ARGS "" CACHE FILEPATH "Prepared native PS5 runtime link response file")
if(NOT EXISTS "${PE2_PS5_LINK_ARGS}")
    message(FATAL_ERROR "PE2_PS5_LINK_ARGS must name the prepared native runtime response file")
endif()
add_compile_definitions(PE2_PS5_HOST=1 PE_PS5=1 __BSD_VISIBLE=1)
# Keep every title object in Ninja's response file, followed by native runtime
# inputs. No payload crt1.o: the application CRT owns _start/constructors/exit.
set(CMAKE_C_LINK_EXECUTABLE "${CMAKE_LINKER} <OBJECTS> -o <TARGET> <LINK_LIBRARIES> @${PE2_PS5_LINK_ARGS}")
set(PE2_PS5_SUPPORT_DIR "" CACHE PATH "Prepared PE2-owned native application support")
if(NOT EXISTS "${PE2_PS5_SUPPORT_DIR}/app_crt.o")
    message(FATAL_ERROR "Prepare the native application support in a PE2-owned build directory first")
endif()
set(PE2_PS5_SDL_SOURCE "" CACHE PATH "Source matching the retained native SDL archive")
set(PE2_PS5_SDL_BUILD "" CACHE PATH "Generated configuration matching the native SDL archive")
foreach(_path "${PE2_PS5_SDL_SOURCE}/src/SDL.c"
    "${PE2_PS5_SDL_BUILD}/include-config-release/build_config/SDL_build_config.h")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing native SDL core input: ${_path}")
    endif()
endforeach()
# Rebuild the real SDL core to omit desktop GTK teardown. Supply explicit
# unsupported process/loadso backends, retaining native video/audio/pad.
file(READ "${PE2_PS5_SDL_SOURCE}/src/SDL.c" _pe2_sdl_core)
string(REPLACE "&& !defined(SDL_PLATFORM_PRIVATE)" "&& !defined(SDL_PLATFORM_PRIVATE) && !defined(PE_PS5)" _pe2_sdl_core "${_pe2_sdl_core}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SDL-pe2-ps5-core.c" "${_pe2_sdl_core}")
add_library(pe2-ps5-sdl-core OBJECT "${CMAKE_CURRENT_BINARY_DIR}/SDL-pe2-ps5-core.c"
    "${PE2_PS5_SDL_SOURCE}/src/loadso/dummy/SDL_sysloadso.c"
    "${PE2_PS5_SDL_SOURCE}/src/process/dummy/SDL_dummyprocess.c"
    "${PE2_PS5_SUPPORT_DIR}/SDL-application-filesystem.c")
target_include_directories(pe2-ps5-sdl-core PRIVATE
    "${PE2_PS5_SDL_SOURCE}/include" "${PE2_PS5_SDL_SOURCE}/src"
    "${PE2_PS5_SDL_SOURCE}/src/filesystem/unix"
    "${PE2_PS5_SDL_BUILD}/include-config-release/build_config"
    "${PE2_PS5_SDL_BUILD}/include-revision")
target_compile_definitions(pe2-ps5-sdl-core PRIVATE SDL_STATIC_LIB USING_GENERATED_CONFIG_H
    SDL_LOADSO_DUMMY=1 SDL_PROCESS_DUMMY=1
    SDL_BUILD_MAJOR_VERSION=3 SDL_BUILD_MINOR_VERSION=4 SDL_BUILD_MICRO_VERSION=16)
