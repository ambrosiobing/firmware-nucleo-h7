# Does this image use c/board/retarget.c's _write, or libnosys's stub?
#
# Run as a post-build step by add_firmware(). It reads the map file, which
# records the object that satisfied each symbol, and fails the build if _write
# came from anywhere but retarget.
#
# The defect this guards against produced no error, only
#
#   warning: _write is not implemented and will always fail
#
# among several similar notes, while printf on the board's only console wrote
# into a stub that returns failure. Written Friday 2 October 2026, the day the
# first successful cross link made the warning visible.
if(NOT EXISTS "${MAPFILE}")
  message(FATAL_ERROR
    "${TARGET_NAME}: no map file at ${MAPFILE}, so which _write got linked "
    "cannot be checked. The link line should carry -Wl,-Map=<name>.map .")
endif()

file(READ "${MAPFILE}" MAP)

# The map lists the archive or object that provided each symbol on the line
# after the symbol's own entry. retarget appears there when ours was used, and
# libnosys when the stub was.
if(MAP MATCHES "libnosys[^\n]*\n?[^\n]*_write")
  message(FATAL_ERROR
    "${TARGET_NAME}: _write resolved to the stub in libnosys, not to "
    "c/board/retarget.c . printf would compile, link, run, and write nothing "
    "to the probe's virtual serial port, which on the bench looks like a "
    "wiring fault rather than a link fault.\n"
    "The cause is static library scan order. Keep the board library an OBJECT "
    "library in CMakeLists.txt, not a STATIC one.")
endif()

if(NOT MAP MATCHES "retarget")
  message(FATAL_ERROR
    "${TARGET_NAME}: retarget does not appear in ${MAPFILE} at all, so this "
    "image has no _write of its own and printf has nowhere to write. The board "
    "library must be an OBJECT library so every one of its objects is linked.")
endif()

message(STATUS "${TARGET_NAME}: _write comes from this repository's retarget.c")
