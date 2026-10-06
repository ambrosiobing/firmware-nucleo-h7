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
reflashed. Every stage is therefore conditional on the one before it.

**And the version of this command that stood here until Sunday 4 October 2026 did
the very thing the paragraph above warns about.** It guarded the configure on the
configure's exit status and the build on the build's, and then ran `Copy-Item` and
printed `built and reflashed` unconditionally. On Sunday 4 October 2026 the probe
disk was not mounted, `Copy-Item` reported `Cannot find drive. A drive with the name
'D' does not exist`, and the next line said `built and reflashed`. Nothing had been
flashed.

The cause is specific and worth knowing, because it will catch anything else
written this way: **`$LASTEXITCODE` reflects native executables only.** `cmake` and
`ninja` set it; `Copy-Item` is a cmdlet and does not touch it. So a guard reading
`$LASTEXITCODE` after a cmdlet is reading the exit status of whatever ran before the
cmdlet, which in this command was a successful `cmake --build`. A cmdlet needs
`-ErrorAction Stop` inside `try`/`catch`, or `$?`, and this one now uses the first.
The drive letter is also found by label rather than assumed, so a board that
enumerated elsewhere is reported rather than silently missed:

    . .\projects\P01-toolchain-first-light\Use-CubeIDEToolchain.ps1; $cfg = $true; if (-not (Test-Path build-fw\CMakeCache.txt)) { cmake -B build-fw -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake"; $cfg = ($LASTEXITCODE -eq 0) }; if (-not $cfg) { Write-Host 'CONFIGURE FAILED, nothing built, board untouched' } else { cmake --build build-fw --target p09-codec; if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED, board untouched, old image still on it' } else { $v = Get-Volume | Where-Object { $_.FileSystemLabel -eq 'NOD_H7A3ZIQ' }; if (-not $v) { Write-Host 'BUILT BUT NOT FLASHED: no volume labelled NOD_H7A3ZIQ, so the board is off, unplugged or still enumerating. The old image is still on it.' } else { $d = "$($v.DriveLetter):\"; try { Copy-Item build-fw\p09-codec.bin $d -Force -ErrorAction Stop; Write-Host "built and copied to $d, the board is programming" } catch { Write-Host "BUILT BUT COPY FAILED, board untouched: $_" } } } }

Note what the success message now says: copied, and the board is programming. Not
"reflashed". The copy is the last thing this laptop can observe; whether the probe
then programmed the part is a question only the console can answer, and P01's
image prints enough to answer it.

## The real target names

Only targets that exist can be built. The chapters' artefact names are older than
the build and several of them name nothing.

| Target                  | Chapter | State |
| ----------------------- | ------- | ----- |
| `p01-first-light`       | 1       | runs on the board |
| `p02-ring-<mode>`, four of them | 2 | runs on the board |
| `p06-sampling-<backend>`, three | 6 | has no independent build check, no CMSIS pack in CI |
| `p09-codec`             | 9       | runs on the board |
| `p09-codec-pad0`, `p09-codec-pad16`, `p09-codec-pad32`, `p09-codec-pad48` | 9 | the same image displaced by 0, 16, 32 and 48 bytes, for the placement experiment. The pads are whole multiples of the 16 byte instrument resolution and not of the 32 byte cache line, so the experiment is able to fail |

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

## Reading what CI did, and what it costs to ask

    python python/tools/read_ci.py                 the recent runs, one line each
    python python/tools/read_ci.py <sha>           the jobs of one commit

This reads the pages under `github.com` rather than `api.github.com`, and the
reason is a mistake. The API allows 60 requests an hour per address without a
token. On Monday 5 October 2026 a twenty second poll waiting for a run to finish
spent 180 of them inside one hour, after which the question the poll had been
asking could not be answered at all until the hour turned over. The run list and
the per commit checks page carry the same facts, are served from a different and
much larger budget, and are server rendered, which was not the expectation: the
Actions view is a React application and the first guess was that a fetch would
return an empty shell. The run rows are in the HTML. The job list on an
individual run page is not, which is why the per commit view is
`/commit/<sha>/checks` and not `/actions/runs/<id>`.

A short SHA is expanded against the local clone before the request goes out,
because `/commit/0e2c748/checks` answers 404 and that reads exactly like a commit
that does not exist.

**How to read a cancelled run, which took an hour to understand on Monday 5
October 2026.** Six runs across two commits and all three workflows each lasted
`15m 2s`. The run list calls every one of them failed. The run page's own icon
calls it cancelled. The jobs say "This job was cancelled" with not one step
having run. A green run of the same three workflows finishes in 32 to 39
seconds, so the duration alone separates the two cases: about fifteen minutes
with no steps is a job that never got a runner. Nothing in this repository asks
for that. There is no `timeout-minutes` and no `concurrency` block in any of
`checks.yml`, `code.yml` or `firmware.yml`, so no workflow edit can change it,
and the five job identifiers in those files account for all five rows. The thing
to do with such a run is to start it again, which every one of the three
workflows allows through `workflow_dispatch` without a new commit.

**What the reader refuses to do.** It parses markup that belongs to GitHub and
can change without notice, and a parser that stops matching returns an empty
list of runs, which reads exactly like a repository with nothing red in it. So
zero parsed rows is a refusal with exit status 2 and the word REFUSED, never an
empty table. It reads no step, no log, no annotation and no artefact, it cannot
start or re-run anything, and it sees only what a signed out visitor sees. For a
step list or a log, open the run in a browser, or spend the API quota
deliberately and once. Nothing in the build or the test suite calls it, for the
reason `python/tools/check_links.py` gives in its own header: a check that needs
the network fails for reasons that have nothing to do with this repository.
