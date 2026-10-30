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
target_compile_options(fpdc PRIVATE
    -O3 -Wall -Wno-unused-parameter -Wno-sign-compare -ffp-contract=off -fno-math-errno
    -fvisibility=hidden)

target_link_libraries(fpdc android log EGL GLESv3 aaudio m)
# native_app_glue provides the entry point; keep it from being dropped by the linker
target_link_options(fpdc PRIVATE "-u ANativeActivity_onCreate")
