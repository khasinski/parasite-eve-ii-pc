cmake_minimum_required(VERSION 3.21..3.31)
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET
   AND (NOT DEFINED CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum supported macOS version")
endif()
project(parasite-eve-ii-pc VERSION 0.2.0 LANGUAGES C)
option(PE2_PS5_HOST "Enable PS5 entry and storage integration (requires a PS5 PsyZ/SDL toolchain)" OFF)
if(PE2_PS5_HOST)
    include(cmake/ps5/host.cmake)
endif()

# Overlay packages and their variants define the same uninitialised globals in several objects. Apple's toolchain merges
# those tentative definitions into one; GCC and Clang elsewhere need -fcommon to do the same, or the link fails with
# multiple definitions.
if(NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fcommon>)
    # The decompiled sources pass PS1-style pointers where the host headers declare other types (docs/PORT_PLAN.md).
    # Clang on macOS only warns; GCC 14+ and Clang 21+ make these errors by default.
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-pointer-types>
                        $<$<COMPILE_LANGUAGE:C>:-Wno-error=int-conversion>
                        $<$<COMPILE_LANGUAGE:C>:-Wno-error=implicit-function-declaration>)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# PsyZ: our fork of the PsyZ PSY-Q replacement (SDL3 video/audio/input, libgte, libcd, MDEC).
set(PSYZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/psyz" CACHE PATH "PsyZ source root")
add_subdirectory(${PSYZ_DIR}/psyz ${CMAKE_CURRENT_BINARY_DIR}/psyz)
target_compile_definitions(psyz PRIVATE __psyz)
if(PE2_PS5_HOST)
    # The HTTP debugger is a desktop development service, not a game dependency.
    get_target_property(_pe2_psyz_sources psyz SOURCES)
    list(FILTER _pe2_psyz_sources EXCLUDE REGEX "src/dbgserver/dbgserver\\.c$")
    set_property(TARGET psyz PROPERTY SOURCES "${_pe2_psyz_sources}")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # The original task/actor workspaces are accessed through several layout views.
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:-fno-strict-aliasing>"
        "$<$<COMPILE_LANGUAGE:C>:-ffunction-sections>"
        "$<$<COMPILE_LANGUAGE:C>:-fdata-sections>")
endif()

set(PE_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/generated)

# The game builds from the committed layout sources (include/generated, src/port/asm_data.c, asset_parts_table.c,
# globals.c): types, sizes and registrations, no game data. The data itself is read from the user's disc images at run
# time. regenerating them needs the extraction tooling, which was removed from the tree and lives at the tag pe2-layout-tooling.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/port/asset_parts_table.c)
    set(_pe_game_default ON)
else()
    set(_pe_game_default OFF)
endif()
option(PE_BUILD_GAME "Build the game executable" ${_pe_game_default})
if(PE_BUILD_GAME)
    include(cmake/game.cmake)
else()
    message(STATUS "PE_BUILD_GAME is OFF: building the host tests only")
endif()
if(PE2_PS5_HOST)
    # Host test executables are not console applications and must not inherit
    # the application entry/link recipe or write console card directories.
    if(NOT PE_BUILD_GAME)
        message(FATAL_ERROR "PE2_PS5_HOST requires PE_BUILD_GAME=ON")
    endif()
    return()
endif()


# Host checks for scripted arrival coordinates and GTE compatibility macros.
add_executable(actor_walk_gap_test EXCLUDE_FROM_ALL tests/actor_walk_gap_test.c)
add_executable(sdk_random_test EXCLUDE_FROM_ALL tests/sdk_random_test.c)
target_include_directories(sdk_random_test PRIVATE include include/decomp)
target_compile_options(sdk_random_test PRIVATE -UNDEBUG)
target_compile_definitions(sdk_random_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(sdk_random_test PRIVATE psyz)
target_include_directories(actor_walk_gap_test PRIVATE include include/decomp)
target_compile_definitions(actor_walk_gap_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(actor_walk_gap_test PRIVATE psyz)

add_executable(screen_negative_test EXCLUDE_FROM_ALL tests/screen_negative_test.c)
target_include_directories(screen_negative_test PRIVATE include include/decomp)
target_compile_definitions(screen_negative_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(screen_negative_test PRIVATE psyz)

add_executable(gte_compat_test EXCLUDE_FROM_ALL tests/gte_compat_test.c)
target_include_directories(gte_compat_test PRIVATE include include/decomp)
target_compile_definitions(gte_compat_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(gte_compat_test PRIVATE psyz)

# Development tool: reads one package straight from a disc image (src/port/disc_assets.c).
add_executable(pe2_discread EXCLUDE_FROM_ALL tools/port/discread.c src/port/disc_assets.c)
target_include_directories(pe2_discread PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(lzss_test EXCLUDE_FROM_ALL tests/lzss_test.c src/port/lzss.c)
target_include_directories(lzss_test PRIVATE include include/decomp src/main)
target_compile_definitions(lzss_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(lzss_test PRIVATE psyz)

add_executable(tmd_model_test EXCLUDE_FROM_ALL tests/tmd_model_test.c src/port/tmd_dispatch.c)
target_include_directories(tmd_model_test PRIVATE include include/decomp src/main)
target_compile_definitions(tmd_model_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(tmd_model_test PRIVATE psyz)

add_executable(tmd_prim_test EXCLUDE_FROM_ALL tests/tmd_prim_test.c src/port/tmd_prim.c src/port/tmd_dispatch.c)
target_include_directories(tmd_prim_test PRIVATE include include/decomp src/main)
target_compile_definitions(tmd_prim_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(tmd_prim_test PRIVATE psyz)

add_executable(tmd_ops_test EXCLUDE_FROM_ALL tests/tmd_ops_test.c src/port/tmd_ops.c src/port/tmd_dispatch.c)
target_include_directories(tmd_ops_test PRIVATE include include/decomp src/main)
target_compile_definitions(tmd_ops_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(tmd_ops_test PRIVATE psyz)


add_executable(tmd_source_test EXCLUDE_FROM_ALL tests/tmd_source_test.c src/port/tmd_source.c)
target_include_directories(tmd_source_test PRIVATE include include/decomp)
target_compile_definitions(tmd_source_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(tmd_source_test PRIVATE psyz)

add_executable(image_workspace_test EXCLUDE_FROM_ALL tests/image_workspace_test.c)
target_include_directories(image_workspace_test PRIVATE include include/decomp)
target_compile_definitions(image_workspace_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(image_workspace_test PRIVATE psyz)

add_executable(tmd_layout_test EXCLUDE_FROM_ALL tests/tmd_layout_test.c src/port/tmd_layout.c)
target_include_directories(tmd_layout_test PRIVATE include include/decomp)
target_compile_definitions(tmd_layout_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(tmd_layout_test PRIVATE psyz)

add_executable(tmd_layout_assets_test EXCLUDE_FROM_ALL tests/tmd_layout_assets_test.c src/port/tmd_layout.c)
target_include_directories(tmd_layout_assets_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(tmd_layout_assets_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(tmd_layout_assets_test PRIVATE psyz)

add_executable(cap_asset_test EXCLUDE_FROM_ALL tests/cap_asset_test.c src/port/cap_asset.c)
target_include_directories(cap_asset_test PRIVATE include include/decomp)
target_compile_definitions(cap_asset_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(cap_asset_test PRIVATE psyz)

add_executable(cap_resource_test EXCLUDE_FROM_ALL tests/cap_resource_test.c src/port/cap_asset.c src/port/cap_resource.c)
target_include_directories(cap_resource_test PRIVATE include include/decomp)
target_compile_definitions(cap_resource_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(cap_resource_test PRIVATE psyz)

add_executable(task_message_test EXCLUDE_FROM_ALL tests/task_message_test.c src/gameplay/task_message.c)
target_include_directories(task_message_test PRIVATE include include/decomp)
target_compile_definitions(task_message_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(task_message_test PRIVATE psyz)

add_executable(task_teardown_test EXCLUDE_FROM_ALL tests/task_teardown_test.c src/main/task.c)
target_include_directories(task_teardown_test PRIVATE include include/decomp src/main)
target_compile_definitions(task_teardown_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(task_teardown_test PRIVATE psyz)
if(APPLE)
    target_link_options(task_teardown_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(task_teardown_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(cd_ring_test EXCLUDE_FROM_ALL tests/cd_ring_test.c)
target_include_directories(cd_ring_test PRIVATE include include/decomp)
target_compile_definitions(cd_ring_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(cd_ring_test PRIVATE psyz)

add_executable(cd_pump_test EXCLUDE_FROM_ALL tests/cd_pump_test.c)
target_include_directories(cd_pump_test PRIVATE include include/decomp)
target_compile_definitions(cd_pump_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(cd_pump_test PRIVATE psyz)

add_executable(sprite_packet_test EXCLUDE_FROM_ALL tests/sprite_packet_test.c)
target_include_directories(sprite_packet_test PRIVATE include include/decomp)
target_compile_definitions(sprite_packet_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(sprite_packet_test PRIVATE psyz)

add_executable(text_palette_test EXCLUDE_FROM_ALL tests/text_palette_test.c src/main/text_palette.c)
target_include_directories(text_palette_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(text_palette_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(text_palette_test PRIVATE psyz)

add_executable(ordering_table_test EXCLUDE_FROM_ALL tests/ordering_table_test.c)
target_compile_definitions(ordering_table_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(ordering_table_test PRIVATE psyz)

add_executable(scene_broadcast_test EXCLUDE_FROM_ALL tests/scene_broadcast_test.c src/gameplay/task_message.c)
add_executable(spu_key_status_test EXCLUDE_FROM_ALL tests/spu_key_status_test.c)
add_executable(spu_voice_attributes_test EXCLUDE_FROM_ALL tests/spu_voice_attributes_test.c src/port/spu_voice_attributes.c)
target_compile_definitions(spu_voice_attributes_test PRIVATE __psyz)
target_link_libraries(spu_voice_attributes_test PRIVATE psyz)
add_executable(sound_slot_start_test EXCLUDE_FROM_ALL tests/sound_slot_start_test.c)
target_include_directories(sound_slot_start_test PRIVATE include include/decomp src/main)
target_compile_definitions(sound_slot_start_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(sound_slot_start_test PRIVATE psyz)
if(APPLE)
    target_link_options(sound_slot_start_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(sound_slot_start_test PRIVATE -Wl,--gc-sections)
endif()
target_compile_definitions(spu_key_status_test PRIVATE __psyz)
target_link_libraries(spu_key_status_test PRIVATE psyz)

target_include_directories(scene_broadcast_test PRIVATE include include/decomp ${PE_GENERATED_DIR} src/main)
target_compile_definitions(scene_broadcast_test PRIVATE __psyz SKIP_ASM)
target_compile_options(scene_broadcast_test PRIVATE -w -Wno-incompatible-function-pointer-types)
target_link_libraries(scene_broadcast_test PRIVATE psyz)
if(APPLE)
    target_link_options(scene_broadcast_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(scene_broadcast_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(observatory_scene_lifecycle_test EXCLUDE_FROM_ALL tests/observatory_scene_lifecycle_test.c)
target_include_directories(observatory_scene_lifecycle_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(observatory_scene_lifecycle_test PRIVATE __psyz SKIP_ASM)
target_compile_options(observatory_scene_lifecycle_test PRIVATE -Wno-incompatible-function-pointer-types)
target_link_libraries(observatory_scene_lifecycle_test PRIVATE psyz)
if(APPLE)
    target_link_options(observatory_scene_lifecycle_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(observatory_scene_lifecycle_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(raster_clock_test EXCLUDE_FROM_ALL tests/raster_clock_test.c)
target_compile_definitions(raster_clock_test PRIVATE __psyz)
target_link_libraries(raster_clock_test PRIVATE psyz)

add_executable(gpu_tpage_test EXCLUDE_FROM_ALL tests/gpu_tpage_test.c)
target_include_directories(gpu_tpage_test PRIVATE include include/decomp)
target_compile_definitions(gpu_tpage_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(gpu_tpage_test PRIVATE psyz)

add_executable(pad_trace_test EXCLUDE_FROM_ALL tests/pad_trace_test.c src/port/pad_trace.c)
target_include_directories(pad_trace_test PRIVATE include)

add_executable(player_target_test EXCLUDE_FROM_ALL tests/player_target_test.c src/gameplay/player_target.c)
target_include_directories(player_target_test PRIVATE include include/decomp)
target_compile_definitions(player_target_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(player_target_test PRIVATE psyz)

add_executable(action_trigger_test EXCLUDE_FROM_ALL tests/action_trigger_test.c src/gameplay/collision_grid.c src/gameplay/object_lists.c)
target_include_directories(action_trigger_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(action_trigger_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(action_trigger_test PRIVATE psyz)
if(APPLE)
    target_link_options(action_trigger_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(action_trigger_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(pair_contact_test EXCLUDE_FROM_ALL tests/pair_contact_test.c src/gameplay/collision_grid.c src/gameplay/object_lists.c)
target_include_directories(pair_contact_test PRIVATE include include/decomp)
target_compile_definitions(pair_contact_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(pair_contact_test PRIVATE psyz)
if(APPLE)
    target_link_options(pair_contact_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(pair_contact_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(scene_sector_test EXCLUDE_FROM_ALL tests/scene_sector_test.c)
target_include_directories(scene_sector_test PRIVATE include include/decomp)
target_compile_definitions(scene_sector_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(scene_sector_test PRIVATE psyz)
if(APPLE)
    target_link_options(scene_sector_test PRIVATE -Wl,-dead_strip)
else()
    # The test replaces CdGetSector with a double; GNU ld rejects the copy inside PsyZ's libcd object.
    target_link_options(scene_sector_test PRIVATE -Wl,--gc-sections -Wl,--allow-multiple-definition)
endif()

add_executable(area_resource_test EXCLUDE_FROM_ALL tests/area_resource_test.c src/gameplay/area_cd.c)
target_include_directories(area_resource_test PRIVATE include include/decomp)
target_compile_definitions(area_resource_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(area_resource_test PRIVATE psyz)

add_executable(item_prompt_lifecycle_test EXCLUDE_FROM_ALL tests/item_prompt_lifecycle_test.c src/gameplay/item_notice.c)
target_include_directories(item_prompt_lifecycle_test PRIVATE include include/decomp)
target_compile_definitions(item_prompt_lifecycle_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(item_prompt_lifecycle_test PRIVATE psyz)

add_executable(caption_packet_test EXCLUDE_FROM_ALL tests/caption_packet_test.c)
target_include_directories(caption_packet_test PRIVATE include include/decomp)
target_compile_definitions(caption_packet_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(caption_packet_test PRIVATE psyz)
if(APPLE)
    target_link_options(caption_packet_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(caption_packet_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(game_over_image_test EXCLUDE_FROM_ALL tests/game_over_image_test.c)
target_include_directories(game_over_image_test PRIVATE include include/decomp)
target_compile_definitions(game_over_image_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(game_over_image_test PRIVATE psyz)
if(APPLE)
    target_link_options(game_over_image_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(game_over_image_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(fountain_trigger_test EXCLUDE_FROM_ALL tests/fountain_trigger_test.c src/gameplay/object_lists.c)
target_include_directories(fountain_trigger_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(fountain_trigger_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(fountain_trigger_test PRIVATE psyz)
if(APPLE)
    target_link_options(fountain_trigger_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(fountain_trigger_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(fixed_log_test EXCLUDE_FROM_ALL tests/fixed_log_test.c src/port/fixed_log.c src/gameplay/object_lists.c)
target_include_directories(fixed_log_test PRIVATE include include/decomp)
target_compile_definitions(fixed_log_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(fixed_log_test PRIVATE psyz)
if(APPLE)
    target_link_options(fixed_log_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(fixed_log_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(rupert_eyes_test EXCLUDE_FROM_ALL tests/rupert_eyes_test.c)
target_include_directories(rupert_eyes_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(rupert_eyes_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(rupert_eyes_test PRIVATE psyz)
if(APPLE)
    target_link_options(rupert_eyes_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(rupert_eyes_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(cap_room_command_test EXCLUDE_FROM_ALL tests/cap_room_command_test.c src/gameplay/cap_commands.c src/port/cap_asset.c)
target_include_directories(cap_room_command_test PRIVATE include include/decomp)
target_compile_definitions(cap_room_command_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(cap_room_command_test PRIVATE psyz)
if(APPLE)
    target_link_options(cap_room_command_test PRIVATE -Wl,-dead_strip)
else()
    target_link_options(cap_room_command_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(memory_card_test EXCLUDE_FROM_ALL tests/memory_card_test.c src/port/memory_card.c)
target_include_directories(memory_card_test PRIVATE include)
target_link_libraries(memory_card_test PRIVATE psyz)

# Host tests: `cmake --build build --target host_tests && ctest --test-dir build`. They need no game data, except
# the ones in PE_DATA_TESTS, which read extracted assets given on the command line and are left to local runs.
add_executable(asset_disc_cache_test EXCLUDE_FROM_ALL tests/asset_disc_cache_test.c src/port/asset_loader.c src/port/asset_registry.c)
target_include_directories(asset_disc_cache_test PRIVATE include)
add_executable(disc_identity_test EXCLUDE_FROM_ALL tests/disc_identity_test.c src/port/disc_assets.c)
target_include_directories(disc_identity_test PRIVATE include)

add_executable(weapon_m950_test EXCLUDE_FROM_ALL tests/weapon_m950_test.c src/weapons/m950/m950.c)
target_include_directories(weapon_m950_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(weapon_m950_test PRIVATE __psyz SKIP_ASM)
target_link_libraries(weapon_m950_test PRIVATE psyz)

add_executable(weapon_m249_test EXCLUDE_FROM_ALL tests/weapon_fire_test.c src/weapons/m249/m249.c)
target_include_directories(weapon_m249_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(weapon_m249_test PRIVATE __psyz SKIP_ASM WEAPON_FN=m249AttackState WEAPON_HEADER="weapons/m249.h" WEAPON_NAME="m249" WEAPON_TRACE=WEAPON_TRACE_m249)
target_link_libraries(weapon_m249_test PRIVATE psyz)

add_executable(weapon_as12_test EXCLUDE_FROM_ALL tests/weapon_fire_test.c src/weapons/as12/as12.c)
target_include_directories(weapon_as12_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(weapon_as12_test PRIVATE __psyz SKIP_ASM WEAPON_FN=as12AttackState WEAPON_HEADER="weapons/as12.h" WEAPON_NAME="as12" WEAPON_TRACE=WEAPON_TRACE_as12)
target_link_libraries(weapon_as12_test PRIVATE psyz)

add_executable(weapon_mp5a5_test EXCLUDE_FROM_ALL tests/weapon_fire_test.c tests/weapon_mp5a5_stubs.c src/weapons/mp5a5/mp5a5.c)
target_include_directories(weapon_mp5a5_test PRIVATE include include/decomp ${PE_GENERATED_DIR})
target_compile_definitions(weapon_mp5a5_test PRIVATE __psyz SKIP_ASM WEAPON_FN=mp5a5AttackState WEAPON_HEADER="weapons/mp5a5.h" WEAPON_NAME="mp5a5" WEAPON_TRACE=WEAPON_TRACE_mp5a5 WEAPON_INDEX=30)
target_link_libraries(weapon_mp5a5_test PRIVATE psyz)

add_executable(anim_cue_latch_test EXCLUDE_FROM_ALL tests/anim_cue_latch_test.c)

enable_testing()
set(PE_DATA_TESTS cap_room_command_test rupert_eyes_test tmd_layout_assets_test text_palette_test
    observatory_scene_lifecycle_test action_trigger_test fountain_trigger_test)
# raster_clock_test asserts the video backend honours VSync, which a headless runner does not.
list(APPEND PE_DATA_TESTS raster_clock_test)
get_property(_pe_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
add_custom_target(host_tests)
foreach(_t IN LISTS _pe_targets)
    if(_t MATCHES "_test$" AND NOT _t IN_LIST PE_DATA_TESTS)
        add_dependencies(host_tests ${_t})
        add_test(NAME ${_t} COMMAND ${_t})
    endif()
endforeach()
