# The firmware build. One target per project that has target code.
#
#   cmake -B build-fw -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -G Ninja
#   cmake --build build-fw
#
# WHERE THIS BUILDS. Not on win11 aquamarine: no arm-none-eabi-gcc, no ninja, no
# ST install root, confirmed by inventory on Friday 2 October 2026. On win11
# skyhorizon it builds, with STM32CubeIDE 2.2.0's bundled gcc 14.3.1, cmake and
# ninja, which are on no PATH until
# projects/P01-toolchain-first-light/Use-CubeIDEToolchain.ps1 is dot-sourced. And
# on the continuous integration runner, which installs its own.
#
# The host half of this repository needs none of that and is built by
# python/tools/build_host.py instead.
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

# The other warning from the first successful link, assessed and then silenced
# with its reason recorded, which is the only acceptable way to silence one:
#
#   warning: <target>.elf has a LOAD segment with RWX permissions
#
# It is true and it is not about this kind of image. .text and .rodata are read
# only in flash, and .data is writable in RAM, but .data's initial image also
# lives in flash and is copied across by the reset handler, so one PT_LOAD
# segment covers read, write and execute at once. That is how every Cortex-M
# image is laid out and the copy is the whole reason .data works.
#
# The warning exists for hosted systems, where a loader enforces that no page
# is both writable and executable. There is no loader here. On this part the
# equivalent protection is the memory protection unit, configured by software
# at run time, which is chapter 19's subject and not something a segment flag
# can express.
#
# It is suppressed, and the guard is the interesting part.
#
# -Wl,--no-warn-rwx-segments silences it and needs binutils 2.39 or newer. Two
# different toolchains build this repository, CubeIDE's bundled 14.3.1 on win11
# skyhorizon and whatever the continuous integration runner installs, so the
# flag cannot simply be assumed.
#
# check_linker_flag, the obvious guard, does not work here and the way it fails
# is worth recording. The toolchain file sets CMAKE_TRY_COMPILE_TARGET_TYPE to
# STATIC_LIBRARY, which it must, because a bare metal try_compile has no startup
# files and no linker script and could not link a test program. But that means
# check_linker_flag never invokes the linker at all: it archives an object with
# ar, which ignores the flag entirely, and reports success for any string it is
# handed. A check that cannot fail is worse than no check, because it reads like
# evidence.
#
# So ld is asked directly instead, which is a question it can actually answer.
# If ld lists the option in its own help output it understands it. If ld is not
# found, or does not list it, the warning simply stays and nothing breaks.
set(RWX_FLAG "--no-warn-rwx-segments")
set(HAVE_NO_WARN_RWX FALSE)
if(CMAKE_LINKER)
  execute_process(COMMAND ${CMAKE_LINKER} --help
                  OUTPUT_VARIABLE LD_HELP
                  ERROR_VARIABLE  LD_HELP_ERR
                  RESULT_VARIABLE LD_HELP_RC
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(LD_HELP_RC EQUAL 0 AND "${LD_HELP}${LD_HELP_ERR}" MATCHES "${RWX_FLAG}")
    set(HAVE_NO_WARN_RWX TRUE)
  endif()
endif()

if(HAVE_NO_WARN_RWX)
  message(STATUS "ld understands ${RWX_FLAG}, so the RWX segment warning is suppressed")
  add_link_options(-Wl,${RWX_FLAG})
else()
  message(STATUS
    "ld does not list ${RWX_FLAG}, so the RWX segment warning will appear once "
    "per target. It is expected and assessed in CMakeLists.txt.")
endif()

# P01's board support: the vector table, the reset handler, the clock, the LEDs,
# the console and the printf retarget. Every firmware target links it. No vendor
# generated code appears here, and the vendor pack is deliberately not vendored
# because its licence differs file by file.
#
# An OBJECT library, not STATIC, and the reason is a defect this repository
# shipped until Friday 2 October 2026.
#
# c/board/retarget.c defines _write, which is what printf eventually calls. As
# a STATIC library that definition was never used. Libraries are scanned in
# order, and when libboard.a was scanned nothing had yet referenced _write: the
# reference comes from printf inside libc_nano.a, which is scanned afterwards.
# So retarget.o was not pulled in, and libnosys.a, arriving last by way of
# -specs=nosys.specs, satisfied _write with a stub that returns failure.
#
# The symptom was not a build error. It was this warning,
#
#   warning: _write is not implemented and will always fail
#
# sitting among several like it, easy to read as a note about the stubs rather
# than as a statement that printf output would go nowhere. On a board whose
# only console is the probe's virtual serial port, that is the difference
# between first light and a silent terminal that looks like broken wiring.
#
# An OBJECT library has no archive and no scan order: every object is part of
# every executable that links it, so our definitions are present before any
# library is consulted and win outright.
add_library(board OBJECT
    c/board/startup.c
    c/board/system.c
    c/board/board.c
    c/board/uart.c
    c/board/cycles.c
    c/board/icache.c
    c/board/clock280.c
    c/board/retarget.c
    # The clock tree decode, which system.c calls and which deliberately does not
    # live under c/board: it touches no register and must stay compilable by a
    # host compiler so four languages can be compared against it. See
    # c/clock/clocktree.h.
    c/clock/clocktree.c)
target_include_directories(board PUBLIC c/board c/clock)

# One firmware target per project that has target code. A project appears here
# when its main.c exists, and not before: an empty target that links and does
# nothing looks like progress and is not.
function(add_firmware name)
    cmake_parse_arguments(FW "" "" "SOURCES;INCLUDES;VENDOR_INCLUDES;DEFINES" ${ARGN})
    add_executable(${name} ${FW_SOURCES})
    target_link_libraries(${name} PRIVATE board)

    # The map file, which P01's chapter asks the reader to reconcile against the
    # size output, and which the continuous integration job already tests for.
    # Nothing requested one until Friday 2 October 2026, so that test had never
    # had a chance to pass.
    target_link_options(${name} PRIVATE -Wl,-Map=${name}.map)
    target_include_directories(${name} PRIVATE ${FW_INCLUDES})

    # VENDOR_INCLUDES are the same directories, passed with -isystem instead of
    # -I, which tells the compiler to hold its warnings about anything inside
    # them. This is not a way of ignoring problems. It is the only way to see
    # them.
    #
    # The warning set this build uses is deliberately severe, -Wconversion and
    # -Wsign-conversion among it, and ST's own CMSIS headers do not compile
    # cleanly under it. cachel1_armv7.h alone produced about seventy-five
    # -Wsign-conversion warnings on Friday 2 October 2026, fifteen distinct
    # lines repeated once per translation unit. Every one is in a file this
    # repository does not own, will not edit, and cannot fix.
    #
    # That matters because of what happened earlier the same day. A real defect,
    # printf writing into a stub that returned failure, announced itself as one
    # warning line and was missed because it sat in a wall of warnings. Seventy
    # five unactionable warnings per build is not noise to be tolerated, it is
    # the exact condition that hid the last defect, maintained on purpose.
    #
    # So vendor headers are silenced and ours stay loud. If a warning appears
    # after this change it is about code in this repository and someone has to
    # act on it.
    target_include_directories(${name} SYSTEM PRIVATE ${FW_VENDOR_INCLUDES})
    target_compile_definitions(${name} PRIVATE ${FW_DEFINES})
    set_target_properties(${name} PROPERTIES
        SUFFIX ".elf"
        LINK_DEPENDS ${LINKER_SCRIPT})

    # The size of every build, every build. A flash figure that only appears when
    # somebody remembers to ask for it is a figure nobody has.
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_SIZE_UTIL} -A $<TARGET_FILE:${name}>
        COMMENT "size of ${name}")

    # Our own _write is the one that got linked, checked rather than assumed.
    #
    # This is the test for the defect fixed above, and it is written as a build
    # step rather than left to a reader's eye because the failure it catches is
    # a warning in a wall of warnings, and its symptom on the bench is a silent
    # console that looks like a wiring fault. If retarget.c ever stops being
    # linked in, printf goes quiet again and this says so by name.
    #
    # The map file is the evidence: it records which object satisfied each
    # symbol. Searching it for retarget is the whole test.
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DMAPFILE=$<TARGET_FILE_DIR:${name}>/${name}.map
                -DTARGET_NAME=${name}
                -P ${CMAKE_SOURCE_DIR}/cmake/check_retarget.cmake
        COMMENT "checking that ${name} uses this repository's _write")

    # A binary to drag onto the probe's mass storage disk, which is the flashing
    # path that needs no tool installed at all.
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.bin
        COMMENT "${name}.bin for the probe's disk")
endfunction()

# P01, first light. Blinks on the reset clock using only settled board facts.
add_firmware(p01-first-light
    SOURCES projects/P01-toolchain-first-light/c/main.c)

# P01, the clock tree. A SECOND target rather than a flag on the first, and the
# reason is the measurements: every cycle figure in this volume was taken at
# 64 MHz, and one image that sometimes raised the clock would invalidate them
# while changing no line of the code that produced them. Two images means a
# reader can always tell which clock a figure came from by which image produced
# it.
#
# It needs no vendor pack. The registers it writes are in c/board, sourced from
# ST's CMSIS header on the win11 skyhorizon demo laptop and recorded at
# BOARD_CLOCK_280_SOURCE, so this builds wherever p01-first-light does.
#
# It links c/instr/lseref.c, which starts the 32.768 kHz crystal. That is listed
# on this target rather than added to the board library on purpose: the crystal
# is a reference for measuring the clock, not part of bringing it up, and every
# other image in this volume has no use for it.
#
# It also links c/instr/freqcount.c since Monday 5 October 2026, for the same
# kind of reason and the opposite conclusion. The counter is not part of raising
# the clock either, but this is the only image in the volume that already starts
# the crystal the counter gates on, so exercising it here costs one flash instead
# of two. P06's targets link it as well and are the ones that will use it in
# anger; this image only asks whether it initialises.
# pwmsrc.c and pwmmath.c join it on Tuesday 6 October 2026. TIM1 channel 3 on
# PE13 is the known frequency the counter needs in order to be shown to count at
# all, and one wire from PE13 to PD12 completes the arrangement. Linked here
# rather than in a separate image because the experiment IS this image: raise the
# clock, start the source, count it against the crystal.
add_firmware(p01-pll280
    SOURCES projects/P01-toolchain-first-light/c/pll280.c
            c/instr/lseref.c
            c/instr/freqcount.c
            c/instr/freqmath.c
            c/instr/pwmsrc.c
            c/instr/pwmmath.c
    INCLUDES c/instr)

# P02, the ring buffer on the target, which is the only place its deliverable can
# be tested. Four targets, one per ordering mode, because the chapter's subject is
# the comparison and no single build can make it: each -DRING_BARRIER selects a
# different pair of macros in c/ring/barrier.h and therefore different generated
# code at the two places that matter.
#
# Mode 0 is the control and is not shippable. It is built on purpose, to see what
# the compiler does with the two stores when nothing forbids reordering them.
#
# Nothing here needs the vendor pack. SysTick and the cycle counter are both
# architectural, defined by ARM for ARMv7-M, so these four build everywhere P01
# does, including on the continuous integration runner.
set(P02_MODES 0        1        2    3)
set(P02_NAMES none compiler  dmb  acqrel)
foreach(i RANGE 3)
    list(GET P02_MODES ${i} P02_MODE)
    list(GET P02_NAMES ${i} P02_NAME)
    add_firmware(p02-ring-${P02_NAME}
        SOURCES projects/P02-ring-buffer/c/main.c c/ring/ring.c
        INCLUDES ${CMAKE_SOURCE_DIR}/c/ring
        DEFINES RING_BARRIER=${P02_MODE})
endforeach()

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

# CMSIS does not know which device it is compiling for. stm32h7xx.h is a
# selector: it reads one of a list of macros and includes the matching device
# header, and with none of them defined it raises
#
#   #error "Please select first the target STM32H7xx device used in your application"
#
# and defines nothing, so every later line fails with RCC, GPIOB and TIM2
# undeclared. Those errors look like a broken include path and are not.
#
# The board carries an STM32H7A3ZIT6Q. The trailing Q is the SMPS package, and it
# has its own device header, stm32h7a3xxq.h, selected by STM32H7A3xxQ. Choosing
# the non-Q macro would compile and would describe a different power
# configuration, which is exactly the class of wrong-part mistake this volume is
# built around, so it is written here once and used everywhere.
set(DEVICE_MACRO STM32H7A3xxQ)

# CMSIS is two header sets in two directories, and pointing at only one of them
# is the next failure after the device macro:
#
#   Drivers/CMSIS/Device/ST/STM32H7xx/Include   the device: RCC, GPIOB, TIM2
#   Drivers/CMSIS/Include                       the core:   core_cm7.h, DWT, SysTick
#
# The device header includes the core one by plain name, so without the second
# directory the build fails with "core_cm7.h: No such file or directory" several
# layers deep in the vendor pack, which reads like a broken pack and is not.
#
# The core directory is derived from the device one rather than asked for twice,
# because they always sit in that fixed relation inside a pack. If a layout ever
# differs, pass CMSIS_CORE_DIR and the derivation is skipped.
set(CMSIS_CORE_DIR "" CACHE PATH
    "Directory holding core_cm7.h. Derived from CMSIS_DEVICE_DIR when empty.")

if(CMSIS_DEVICE_DIR AND NOT CMSIS_CORE_DIR)
  get_filename_component(_cmsis_root "${CMSIS_DEVICE_DIR}/../../../.." ABSOLUTE)
  set(CMSIS_CORE_DIR "${_cmsis_root}/Include")
  message(STATUS "CMSIS core headers derived as: ${CMSIS_CORE_DIR}")
endif()

if(CMSIS_DEVICE_DIR AND NOT EXISTS "${CMSIS_CORE_DIR}/core_cm7.h")
  message(FATAL_ERROR
    "core_cm7.h was not found at ${CMSIS_CORE_DIR} . "
    "That directory is normally <pack>/Drivers/CMSIS/Include, beside the Device "
    "directory that CMSIS_DEVICE_DIR points into. Pass it explicitly with "
    "-DCMSIS_CORE_DIR=... if this pack is laid out differently.")
endif()

if(NOT CMSIS_DEVICE_DIR)
  message(STATUS
    "CMSIS_DEVICE_DIR is not set, so P06's three targets are skipped. P01 and "
    "P09 do not need it and are built.")
  message(STATUS
    "That is also what happens in continuous integration, which passes no pack, "
    "so P06's three targets are built by exactly one toolchain on exactly one "
    "machine and nothing independent checks them. A cost of not vendoring the "
    "pack, noticed on Friday 2 October 2026 and not enumerated when that was "
    "decided.")
endif()

# P06, the 1 kHz sampling comparison: one application, three acquisition back
# ends, selected at build time so that the comparison is of mechanisms rather
# than of three programs.
foreach(BACKEND systick timer dma)
    if(NOT CMSIS_DEVICE_DIR)
        continue()
    endif()
    if(BACKEND STREQUAL "dma")
        set(ACQ projects/P06-timer-sampling/c/acq_dma_double.c)
    else()
        set(ACQ projects/P06-timer-sampling/c/acq_${BACKEND}.c)
    endif()
    add_firmware(p06-sampling-${BACKEND}
        SOURCES projects/P06-timer-sampling/c/main.c
                projects/P06-timer-sampling/c/marker.c
                # adc_bringup.c, added Friday 9 October 2026. The converter's
                # eight steps left acq_timer.c so that all three back ends can
                # reach the same converter: only the trigger differs between
                # them, and three copies of a sourced register sequence drift
                # without anything noticing. One entry here serves all three
                # targets because this foreach shares its SOURCES list.
                projects/P06-timer-sampling/c/adc_bringup.c
                c/instr/cyccnt.c
                c/instr/freqcount.c
                c/instr/freqmath.c
                # lseref.c, added Tuesday 6 October 2026, because freqcount.c
                # calls into it for the crystal gate and these three targets
                # could not link without it. Nothing had caught that: they are
                # gated behind CMSIS_DEVICE_DIR, CI passes no pack so CI skips
                # them, and the one laptop with a pack had never been asked to
                # build them. python/tools/check_link_closure.py now checks this
                # for every target.
                c/instr/lseref.c
                # adcmath.c and pwmmath.c, added Wednesday 7 October 2026 when
                # acq_timer.c started calling into them: the converter bring-up
                # takes its boost setting and preselection bit from the first
                # and its timer clock from the second, and without these the
                # three targets have undefined symbols. Found by
                # check_link_closure.py, but only after two defects in that
                # check were fixed in the same commit: a set() whose value held
                # ${...} contributed nothing, so two of these three back ends
                # had never been followed at all, and a quoted include of a
                # header in another first-party directory did not resolve, so no
                # c/instr include from a project file had ever been followed.
                c/instr/adcmath.c
                c/instr/pwmmath.c
                ${ACQ}
        INCLUDES projects/P06-timer-sampling/c c/instr
        VENDOR_INCLUDES ${CMSIS_DEVICE_DIR} ${CMSIS_CORE_DIR}
        DEFINES ${DEVICE_MACRO})
endforeach()

# P09, the codec on the target, checking the negative golden vector there rather
# than only on the host.
# Chapter 9 three times over, identical in every respect but where its code sits.
#
# projects/P09-payload-codec/c/pad.c contributes a run of zero bytes to the code section and is listed
# FIRST, so everything after it is displaced by that many bytes. The encoder is
# byte for byte the same in all three; only its address changes. Each image reports
# the encoder cold and cached, so the pair of predictions in pad.c's comment can be
# checked: cold figures should differ between the three, cached figures should
# agree.
#
# Source order is load bearing here. The linker script takes *(.text) then
# *(.text*) and within a glob the order is the order of the objects on the link
# line, which is this list. Moving pad.c after main.c would displace nothing and
# the experiment would quietly become three identical builds.
# Expressed in multiples of 16 because that is the resolution that exists: whatever
# follows .text.p09pad is aligned to 16 bytes, so the linker rounds any other pad
# up and the displacement stops equalling the pad. Here they are equal.
#
# Two residues modulo 32 across four images: shifts 0 and 32 at residue 0, shifts
# 16 and 48 at residue 16. Within-pair agreement is the control; between-pair
# difference is the effect. pad.c explains what each outcome would mean.
#
# Two earlier sets failed the size gate before reaching the board. 0, 64, 160 were
# all whole cache lines, so every image had identical alignment. 0, 4, 12, 20, 36
# rounded to 0, 16, 16, 32, 48, making two of them the same image.
set(P09_PADS 0 16 32 48)
foreach(PAD IN LISTS P09_PADS)
    if(PAD EQUAL 0)
        set(P09_TARGET p09-codec)      # the reference build, pad.c emits nothing
    else()
        set(P09_TARGET p09-codec-pad${PAD})
    endif()
    add_firmware(${P09_TARGET}
        SOURCES projects/P09-payload-codec/c/pad.c projects/P09-payload-codec/c/main.c c/payload/payload.c
        INCLUDES ${CMAKE_SOURCE_DIR}/c/payload
        DEFINES P09_PAD_BYTES=${PAD})
endforeach()
