# SPDX-License-Identifier: CC0-1.0
set(PE2_PS5_PROVIDER_SYSROOT "" CACHE PATH "Headers matching the linked native ps5platform provider")
if(NOT EXISTS "${PE2_PS5_PROVIDER_SYSROOT}/include/ps5platform/libc.h")
    message(FATAL_ERROR "PE2 native directory bridge needs the matching provider sysroot")
endif()
set(_pe2_dir "${CMAKE_CURRENT_SOURCE_DIR}/platform/ps5/directory")
set(_pe2_provider_object "${CMAKE_CURRENT_BINARY_DIR}/pe2-native-directory-provider.o")
add_custom_command(OUTPUT "${_pe2_provider_object}"
    COMMAND "${PE2_LLVM_BIN}/clang.exe" -target x86_64-sie-ps5
        "--sysroot=${PE2_PS5_PROVIDER_SYSROOT}"
        -isystem "${PE2_PS5_PROVIDER_SYSROOT}/include"
        -I "${_pe2_dir}/provider-headers"
        -std=gnu11 -fPIC -fno-stack-protector -femulated-tls -Wall -Wextra -Werror
        -c "${_pe2_dir}/pe-native-directory-bridge.c" -o "${_pe2_provider_object}"
    DEPENDS "${_pe2_dir}/pe-native-directory-bridge.c" "${_pe2_dir}/provider-headers/errno.h"
    VERBATIM)
target_sources(parasite-eve-ii-pc PRIVATE "${_pe2_provider_object}"
    "${_pe2_dir}/pe-game-directory.c" "${_pe2_dir}/directory-wrap.c")
target_link_libraries(parasite-eve-ii-pc PRIVATE
    --wrap=opendir --wrap=readdir --wrap=closedir --wrap=scandir)
