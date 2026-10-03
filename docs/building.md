# Building and flashing this repository

This is the procedure that works, written down on Saturday 3 October 2026 after a
hand-over command failed twice in a row on the only laptop that can build. Every
command in a chapter should agree with this file; where a chapter's project is not
built yet, its commands are a plan and the artefact names in them are placeholders.

## Where it builds, and where it does not

The cross build runs on one machine: the win11 skyhorizon demo laptop. The cross
compiler, CMake and Ninja all come bundled inside STM32CubeIDE 2.2.0 and none of
them is registered system-wide, so a fresh terminal cannot run even `cmake`.

The win11 aquamarine authoring laptop has a host gcc, bundled with Qt. It is not
used. Authoring happens there, compiling does not.

## The two directories, which are not interchangeable

| Directory    | What goes in it                      | Generator |
| ------------ | ------------------------------------ | --------- |
| `build-fw`   | the cross build, firmware for the part | Ninja   |
| `build-host` | the host build, tests and shared libraries | default |

Both are in `.gitignore`, so each machine has its own and neither is ever pushed.
A missing one is normal on a fresh clone and is not a fault.

`build` is not used by anything. Nineteen chapters named it until Saturday 3
October 2026, which is how a hand-over command came to fail with

    Error: .../build is not a directory

## The sequence

Put the bundled toolchain on PATH first. Dot-source it; running it sets PATH in a
child scope that exits immediately, which looks exactly like the script having
done nothing.

    . .\projects\P01-toolchain-first-light\Use-CubeIDEToolchain.ps1

Configure, once per clone. It is harmless to repeat.

    cmake -B build-fw -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake"

The quotes around the whole `-D` argument are required, not decoration. Without
them PowerShell 5.1 splits the argument at the final dot and CMake receives the
toolchain file as `cmake/arm-none-eabi` with `.cmake` reported separately as an
ignored extra path. The failure then reads as a missing file rather than as a
quoting problem, and the next error after it, `CMAKE_C_COMPILER not set`, points
away from the real cause.

Build one target.

    cmake --build build-fw --target p09-codec

## Flashing

The on-board debugger presents a USB mass storage disk. Copying a raw binary onto
it programs the part and resets it. There is no separate flashing tool and none is
needed.

On the win11 skyhorizon demo laptop the disk is `D:`, labelled `NOD_H7A3ZIQ`.

    Copy-Item build-fw\p09-codec.bin D:\ -Force

`probe-rs` and `openocd` are both reasonable tools for this part and neither has
been used in this repository. Chapters referred to `probe-rs run` until Saturday 3
October 2026; that was written from convention, never run here, and has been
replaced by the copy above.

## Why each stage is guarded in a hand-over

PowerShell's `;` does not stop on failure. A sequence that configures, builds and
copies will copy a stale binary after a failed compile and then print a success
message, which produces a board running old firmware while the terminal says it was
reflashed. Every stage is therefore conditional on the one before it:

    . .\projects\P01-toolchain-first-light\Use-CubeIDEToolchain.ps1; $cfg = $true; if (-not (Test-Path build-fw\CMakeCache.txt)) { cmake -B build-fw -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake"; $cfg = ($LASTEXITCODE -eq 0) }; if ($cfg) { cmake --build build-fw --target p09-codec; if ($LASTEXITCODE -eq 0) { Copy-Item build-fw\p09-codec.bin D:\ -Force; Write-Host 'built and reflashed' } else { Write-Host 'BUILD FAILED, board untouched, old image still on it' } } else { Write-Host 'CONFIGURE FAILED, nothing built, board untouched' }

## The real target names

Only targets that exist can be built. The chapters' artefact names are older than
the build and several of them name nothing.

| Target                  | Chapter | State |
| ----------------------- | ------- | ----- |
| `p01-first-light`       | 1       | runs on the board |
| `p02-ring-<mode>`, four of them | 2 | runs on the board |
| `p06-sampling-<backend>`, three | 6 | has no independent build check, no CMSIS pack in CI |
| `p09-codec`             | 9       | runs on the board |

Everything else in the volume is not built. Where such a chapter shows a command,
read it as a plan.

## Reading the console

115200 baud, 8N1, no flow control, on COM13 on the win11 skyhorizon demo laptop.

    $p = New-Object System.IO.Ports.SerialPort COM13,115200,None,8,one; $p.Open(); $end = (Get-Date).AddSeconds(20); while ((Get-Date) -lt $end) { if ($p.BytesToRead -gt 0) { Write-Host -NoNewline $p.ReadExisting() }; Start-Sleep -Milliseconds 100 }; $p.Close()

Open the port before the reset that produces the output you want, or the start of
the report is gone. The port buffers, so output that appears late may have been
produced earlier; a capture without a timestamp cannot establish when a board did
something, which has already produced one wrong conclusion about the part resetting
every three seconds.
