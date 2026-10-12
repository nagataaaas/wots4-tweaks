cmake_minimum_required(VERSION 3.25)
project(wots4_mod_tweaks LANGUAGES C CXX)

# When building standalone, pass -DWOTS4_SDK_DIR=<path to wots4-mods>
if(NOT COMMAND wots4_add_mod)
    set(WOTS4_SDK_DIR "" CACHE PATH "Path to wots4-mods (ModLoader)")
    if(NOT EXISTS "${WOTS4_SDK_DIR}/cmake/Wots4.cmake")
        message(FATAL_ERROR "Set WOTS4_SDK_DIR to the path of wots4-mods")
    endif()
    include(${WOTS4_SDK_DIR}/cmake/Wots4.cmake)
    enable_testing()
    set(WOTS4_BUILD_TESTS ON)
endif()

wots4_use_minhook()

# The entry point and the options menu, shared by mod.dll and the tests
set(TWEAKS_CORE_SOURCES
    src/mod.cpp
    src/options_menu.cpp)
# Each tweak adds its sources in src/<tweak name>/ here
set(TWEAKS_TWEAK_SOURCES
    src/tweak_list.cpp
    src/game_code.cpp
    src/laura_yobai/laura_yobai.cpp
    src/loot_drop/loot_drop.cpp
    src/custom_style_limit/custom_style_limit.cpp
    src/instant_job/instant_job.cpp
    src/script_patch/script_patch.cpp
    src/persuade_success/persuade_success.cpp
    src/smith_best_roll/smith_best_roll.cpp
    src/recruit_success/recruit_success.cpp)

# Use the Mod's folder name in the game folder (mods/<name>/) as <name>
wots4_add_mod(tweaks SOURCES ${TWEAKS_CORE_SOURCES} ${TWEAKS_TWEAK_SOURCES} DEF src/mod.def
    LIBS wots4_minhook
    LICENSES docs/licenses/minhook.txt
    NO_RELEASE)
if(WOTS4_BUILD_TESTS)
    # The test tweak list stands in for the real tweaks
    wots4_add_probe(tweaks_options_menu_tests
        SOURCES ${TWEAKS_CORE_SOURCES} tests/options_menu_tests.cpp tests/test_tweak_list.cpp)
    # Checks, with synthetic definitions, how the Laura mark is set for each setting
    wots4_add_probe(laura_yobai_probe SOURCES tests/laura_yobai_probe.cpp)
    # Each native probe loads mod.dll into a copy of the exe mapped in its own process and runs
    # the tweak's hooked code with synthetic game objects (the game is not started)
    set(WOTS4_UNPACKED_EXE "${WOTS4_ROOT}/research/bin/WayOfTheSamurai4.unpacked.exe" CACHE FILEPATH "Decrypted exe")
    file(TO_NATIVE_PATH "${WOTS4_UNPACKED_EXE}" unpacked_native)
    # The game's data files that some probes copy into memory (read only)
    set(WOTS4_GAME_DIR "D:/SteamLibrary/steamapps/common/Way of the Samurai 4" CACHE PATH "Game installation (read only)")
    function(tweaks_add_native_probe name)
        wots4_add_probe(${name} SOURCES tests/${name}.cpp
            LINK_OPTIONS /BASE:0x10000000 /DYNAMICBASE
            ARGS ${unpacked_native} $<TARGET_FILE:mod_tweaks> ${ARGN} NEEDS_EXE)
        add_dependencies(${name} mod_tweaks)
    endfunction()
    tweaks_add_native_probe(laura_yobai_native_probe)
    tweaks_add_native_probe(loot_drop_native_probe)
    tweaks_add_native_probe(custom_style_limit_native_probe)
    tweaks_add_native_probe(instant_job_native_probe)
    set(script_folder "${WOTS4_GAME_DIR}/Common/ActionScript")
    tweaks_add_native_probe(persuade_success_native_probe
        "${script_folder}/Script/AsScriptPack.l" "${script_folder}/UserCommand/AsUserCmdPack.l")
    tweaks_add_native_probe(smith_best_roll_native_probe)
    tweaks_add_native_probe(recruit_success_native_probe "${script_folder}/Script/AsScriptPack.l")
endif()
