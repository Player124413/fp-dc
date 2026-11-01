cmake_minimum_required(VERSION 3.18)
# Freedom Planet - Android port. Everything is C: the game (src/game, untouched by the port), the
# OpenGL ES 3 renderer, and the platform layer + shell (launcher, settings, touch controls editor).
project(fpdc C)

set(CMAKE_C_STANDARD 99)
set(FP "${CMAKE_CURRENT_LIST_DIR}/../../../../..")

file(GLOB GAME_SRC "${FP}/src/game/*.c")

set(FP_SRC
    ${GAME_SRC}
    ${FP}/src/resources/assets.c
    ${FP}/src/resources/pack.c
    ${FP}/src/physics/level.c
    ${FP}/src/physics/sensors.c
    ${FP}/src/physics/fmath.c
    ${FP}/src/characters/player.c
    ${FP}/src/renderer/render_gles.c
    ${FP}/src/renderer/ui_gles.c
    ${FP}/src/renderer/twiddle.c
    ${FP}/src/platform/android/plat_android.c
    ${FP}/src/platform/android/snd_android.c
    ${FP}/src/platform/android/shell.c
    ${FP}/src/platform/android/launcher.c
    ${FP}/src/platform/android/ctl_editor.c
    ${FP}/src/platform/android/pad_draw.c
    ${FP}/src/platform/android/widgets.c
    ${FP}/src/platform/android/touch.c
    ${FP}/src/platform/android/cfg.c
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

add_library(fpdc SHARED ${FP_SRC})

target_include_directories(fpdc PRIVATE
    ${FP}/src
    ${FP}/src/platform/android
    ${ANDROID_NDK}/sources/android/native_app_glue)

# The Dreamcast build is compiled with -ffp-contract=off so that the deterministic physics behaves
# the same everywhere; keep that, and let the compiler vectorize freely otherwise.
# no -fvisibility=hidden: native_app_glue's ANativeActivity_onCreate must stay exported, or Android
# cannot find the entry point of the library
target_compile_options(fpdc PRIVATE
    -O3 -Wall -Wno-unused-parameter -Wno-sign-compare -ffp-contract=off -fno-math-errno)

# A crash report from a phone carries addresses, not source text. Keep the symbol table (and line
# info) in the 64-bit build, which is the one every phone that sends reports runs: the handler logs
# <module>+<offset>, and app/build.gradle keeps this .so from being stripped, so
#     addr2line -f -C -e libfpdc.so 0x2a1b4
# resolves the report offline against the very build that crashed. The 32-bit and x86_64 builds stay
# lean; they are not what a player reports from.
if (ANDROID_ABI STREQUAL "arm64-v8a")
    target_compile_options(fpdc PRIVATE -g)
endif ()

target_link_libraries(fpdc android log EGL GLESv3 aaudio m)
# native_app_glue provides the entry point; keep it from being dropped by the linker
target_link_options(fpdc PRIVATE "-u ANativeActivity_onCreate")
