# Game-neutral co-simulation B-side (reference interpreter) for FF6.
#
# Mirrors the `smw_cosim_ref` target in snesrecomp/cosim/CMakeLists.txt: runner
# DEVICE sources + interp816 + the shared cosim engine + the headless accurate
# frame driver. Deliberately excludes the recompiled runtime (common_rtl.c,
# cpu_state.c, generated banks, main.c) - this side executes the ROM through
# interp816, and hashing the same device structs as the A side makes the two
# states comparable.
#
# DEV/DIAGNOSTICS ONLY (docs/COSIM.md "Production discipline").
cmake_minimum_required(VERSION 3.20)
project(ff6_cosim_ref C)

set(CMAKE_C_STANDARD 11)
set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../snesrecomp" CACHE PATH
    "snesrecomp checkout")
set(RUNSNES "${SNESRECOMP_ROOT}/runner/src")

add_executable(ff6_cosim_ref
    ${SNESRECOMP_ROOT}/cosim/ref_driver.c
    ref_glue.c
    ${RUNSNES}/cosim.c
    ${RUNSNES}/cosim_state.c
    ${RUNSNES}/audio_trace.c
    ${RUNSNES}/crc32.c
    ${RUNSNES}/sha256.c
    ${RUNSNES}/util.c
    ${RUNSNES}/snes/apu.c
    ${RUNSNES}/snes/cart.c
    ${RUNSNES}/snes/sa1.c
    ${RUNSNES}/snes/sdd1.c
    ${RUNSNES}/snes/cx4.c
    ${RUNSNES}/snes/cpu.c
    ${RUNSNES}/snes/dma.c
    ${RUNSNES}/snes/dsp.c
    ${RUNSNES}/snes/dsp1.c
    ${RUNSNES}/snes/dsp1_hle.c
    ${RUNSNES}/snes/joypad.c
    ${RUNSNES}/snes/ppu.c
    ${RUNSNES}/snes/ppu_legacy.c
    ${RUNSNES}/snes/snes.c
    ${RUNSNES}/snes/snes_other.c
    ${RUNSNES}/snes/spc.c
    ${RUNSNES}/snes/superfx.c
    ${RUNSNES}/snes/ws_shadow.c
    ${RUNSNES}/snes/interp816.c
    ${RUNSNES}/snes/color_lut.c
)

target_include_directories(ff6_cosim_ref PRIVATE
    ${RUNSNES} ${RUNSNES}/snes
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src)   # game variables.h
target_compile_definitions(ff6_cosim_ref PRIVATE
    SNES_COSIM SNES_COSIM_REF SNESRECOMP_TRACE=0 SDL_MAIN_HANDLED)
if(NOT MSVC)
    target_compile_options(ff6_cosim_ref PRIVATE -w -Wno-implicit-function-declaration)
endif()
if(NOT MSVC)
    target_link_libraries(ff6_cosim_ref PRIVATE m)
endif()
if(WIN32)
    target_link_libraries(ff6_cosim_ref PRIVATE ws2_32)
endif()
