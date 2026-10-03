# cmake/arm-none-eabi.cmake: the toolchain file, not the project file.
#
# P01 owns this and every project that targets the board uses it:
#
#   cmake -B build-fw -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -G Ninja
#   cmake --build build-fw
#
# Where this runs. On win11 skyhorizon, with the gcc 14.3.1, cmake and ninja
# bundled inside CubeIDE 2.2.0, which are on no PATH until
# projects/P01-toolchain-first-light/Use-CubeIDEToolchain.ps1 is dot sourced.
# The first images built this way ran on the board on Friday 2 October 2026.
# On win11 aquamarine there is no cross toolchain and none is to be installed:
# that laptop authors the volume and runs the host suite, and compiles nothing.
#
# The one line that matters most is CMAKE_TRY_COMPILE_TARGET_TYPE. Without it
# CMake tests the compiler by building and linking an executable, which fails on
# a bare metal target because there is no startup code or linker script yet at
# that point, and the error it prints blames the compiler rather than the test.
# Every embedded CMake setup hits this once.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Build a static library to test the compiler, not an executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PREFIX arm-none-eabi-)

find_program(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++ REQUIRED)
find_program(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc REQUIRED)
find_program(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy REQUIRED)
find_program(CMAKE_SIZE_UTIL    ${TOOLCHAIN_PREFIX}size REQUIRED)
find_program(CMAKE_OBJDUMP      ${TOOLCHAIN_PREFIX}objdump)

# Not used to link anything: gcc does the linking, as it must, because it alone
# knows where the C library and the startup files are. This is here so the build
# can ask ld what options it understands, which is how the root CMakeLists.txt
# decides whether a newer linker warning can be suppressed. Not REQUIRED,
# because a missing ld means only that the question goes unasked.
find_program(CMAKE_LINKER        ${TOOLCHAIN_PREFIX}ld)

# Look for programs on the host, and for headers and libraries only in the
# toolchain. Without this a find_package can hand a bare metal build a host
# library, which links and then does not run.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The part. Cortex-M7 with a double precision floating point unit, hardware
# calling convention. fpv5-d16 rather than fpv5-sp-d16 because this part has the
# double precision unit; using the single precision flag would silently push
# double arithmetic into software.
#
# This core also has the digital signal processing instruction extension, which
# is what makes the optimised kernels of P18 worth anything here. It does NOT
# have the vector extension that the impressive published figures for this
# family are measured on: that belongs to later cores. Saying so here because
# this is the file where somebody would try to enable it.
set(MCU_FLAGS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard)

add_compile_options(${MCU_FLAGS})
add_link_options(${MCU_FLAGS})

# Per-configuration flags. -Og rather than -O0 for debugging, because -O0 on this
# core produces code so much larger and slower that timing behaviour differs from
# anything shippable, and a debug build whose timing is unrepresentative is a
# debug build that hides the interesting defects.
set(CMAKE_C_FLAGS_DEBUG          "-Og -g3" CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE        "-Os -g"  CACHE STRING "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os -g3" CACHE STRING "")
