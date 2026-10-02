# The firmware build. One target per project that has target code.
#
#   cmake -B build-fw -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -G Ninja
#   cmake --build build-fw
#
# NOT RUNNABLE YET on win11 aquamarine: no arm-none-eabi-gcc, no flashing tool
# and no ninja as of Thursday 1 October 2026. The host half of this repository
# needs none of them and is built by python/tools/build_host.py instead.
#
# This file deliberately refuses to configure for the host. A bare metal project
# that silently produces a host binary named like firmware is worse than one that
# will not configure: somebody flashes nothing and wonders why the board is
# quiet.
cmake_minimum_required(VERSION 3.22)

# The cross-compiling check has to come AFTER project(), because
# CMAKE_CROSSCOMPILING is one of the variables project() sets. Putting it before,
# as the first version of this file did, makes it always false and the build
# refuses even when the toolchain file was supplied. The symptom is a
# FATAL_ERROR telling you to pass the very argument you just passed, which is a
# confusing enough failure to be worth this comment. Found on
# Friday 2 October 2026, on the first cross build ever attempted here.
project(nucleo-h7a3-firmware C ASM)

if(NOT CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR
    "This builds firmware for the NUCLEO-H7A3ZI-Q and needs the cross "
    "toolchain, which the toolchain file supplies:\n"
    "  cmake -B build-fw -G Ninja -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/arm-none-eabi.cmake\n"
    "Pass that as an absolute path: a relative one resolves against the build "
    "directory in some CMake versions and the source directory in others.\n"
    "For the host tests, which need none of that, use:\n"
    "  python python/tools/build_host.py ; python -m pytest python/tests -q")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(WARNINGS
    -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion
    -Wdouble-promotion -Wpedantic)

add_compile_options(${WARNINGS} -ffunction-sections -fdata-sections -fno-common)

# The linker script is P01's and lives in c/ld/, because every project links
# the same one. Changing it is a decision about every project at once.
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/c/ld/stm32h7a3zi.ld)

add_link_options(
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -specs=nano.specs
    -specs=nosys.specs)

# P01's board support: the vector table, the reset handler, the clock, the LEDs,
# the console and the printf retarget. Every firmware target links it. No vendor
# generated code appears here, and the vendor pack is deliberately not vendored
# because its licence differs file by file.
add_library(board STATIC
    c/board/startup.c
    c/board/system.c
    c/board/board.c
    c/board/uart.c
    c/board/retarget.c)
target_include_directories(board PUBLIC c/board)

# One firmware target per project that has target code. A project appears here
# when its main.c exists, and not before: an empty target that links and does
# nothing looks like progress and is not.
function(add_firmware name)
    cmake_parse_arguments(FW "" "" "SOURCES;INCLUDES;DEFINES" ${ARGN})
    add_executable(${name} ${FW_SOURCES})
    target_link_libraries(${name} PRIVATE board)
    target_include_directories(${name} PRIVATE ${FW_INCLUDES})
    target_compile_definitions(${name} PRIVATE ${FW_DEFINES})
    set_target_properties(${name} PROPERTIES
        SUFFIX ".elf"
        LINK_DEPENDS ${LINKER_SCRIPT})

    # The size of every build, every build. A flash figure that only appears when
    # somebody remembers to ask for it is a figure nobody has.
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_SIZE_UTIL} -A $<TARGET_FILE:${name}>
        COMMENT "size of ${name}")

    # A binary to drag onto the probe's mass storage disk, which is the flashing
    # path that needs no tool installed at all.
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.bin
        COMMENT "${name}.bin for the probe's disk")
endfunction()

# P01, first light. Blinks on the reset clock using only settled board facts.
add_firmware(p01-first-light
    SOURCES c/P01/main.c)

# P06 needs the vendor device header, stm32h7xx.h, which this repository
# deliberately does not vendor: that pack's licence differs file by file and a
# project that pasted it in would be redistributing terms it had not read. So its
# targets exist only when told where the pack was unpacked, and P01 and P09 build
# without it because their register access goes through c/board/stm32h7a3_regs.h,
# which is this repository's own.
#
#   cmake -B build-fw -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -G Ninja #     -DCMSIS_DEVICE_DIR=<where the pack was unpacked>
set(CMSIS_DEVICE_DIR "" CACHE PATH
    "Directory holding stm32h7xx.h and the CMSIS core headers. P06 needs it.")

if(NOT CMSIS_DEVICE_DIR)
  message(STATUS
    "CMSIS_DEVICE_DIR is not set, so P06's three targets are skipped. P01 and "
    "P09 do not need it and are built.")
endif()

# P06, the 1 kHz sampling comparison: one application, three acquisition back
# ends, selected at build time so that the comparison is of mechanisms rather
# than of three programs.
foreach(BACKEND systick timer dma)
    if(NOT CMSIS_DEVICE_DIR)
        continue()
    endif()
    if(BACKEND STREQUAL "dma")
        set(ACQ c/P06/acq_dma_double.c)
    else()
        set(ACQ c/P06/acq_${BACKEND}.c)
    endif()
    add_firmware(p06-sampling-${BACKEND}
        SOURCES c/P06/main.c
                c/P06/marker.c
                c/instr/cyccnt.c
                c/instr/freqcount.c
                ${ACQ}
        INCLUDES c/P06 c/instr ${CMSIS_DEVICE_DIR})
endforeach()

# P09, the codec on the target, checking the negative golden vector there rather
# than only on the host.
add_firmware(p09-codec
    SOURCES c/P09/main.c c/payload/payload.c
    INCLUDES ${CMAKE_SOURCE_DIR}/c/payload)
