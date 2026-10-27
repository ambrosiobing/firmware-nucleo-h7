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

**A `build-host` older than its sources is a fault, and since Friday 9 October 2026
the test session refuses to start on one.** `python/tests/conftest.py` compares
every artefact under `build-host` with the newest C or C++ source and
`build_host.py` itself, and every Rust filter under `target/release` with the
newest Rust source, and stops with the artefact, the source and the build command
named. Over-approximate on purpose: any source newer than any artefact marks the
set stale, because `build_host.py` rebuilds everything in seconds and a per-artefact
source list would drift. In CI the build runs immediately before the tests and the
guard never fires. In WSL on win11 skyhorizon it fires after every `git pull` that
touched a source, until the two commands below are run, which is the case that
produced it: the suite there compared a freshly pulled `rate.py` against a
`librate.so`, a `rate_filter` and a `p06-filter` built from the previous source, and
reported "Python says edges=99 and C says 49" as a disagreement between four
languages.

    python3 python/tools/build_host.py
    cargo build --release --workspace

**And on its first run the guard found a week-old instance on the authoring
laptop.** win11 aquamarine, which has not been allowed to compile since Saturday
3 October 2026, still held a `build-host` of 27 files stamped Thursday 2 October
2026 08:35, built by the Qt-bundled gcc the day before the rule: `payload.dll`,
`frame.dll`, `node_sm.dll`, `ring0.dll` to `ring3.dll`, `cpp_filter.exe` and the
property-test executables. Every pass and skip count quoted from that laptop in the
week between was taken with those binaries loaded. They are out of the tree now, and
without them the suite there read 156 passing, 146 skipping and **63 failing**,
fifty-four failures and nine errors at setup: sixty-three tests written in the days
that laptop compiled assumed the artefact existed rather than skipping without it.

**Since Friday 16 October 2026 one rule decides, `require_artefact` in
`python/tests/conftest.py`.** A missing artefact where `build-host` does not exist
at all is a laptop that cannot compile, and the test skips naming the WSL command;
a missing artefact where `build-host` exists is a build that drifted, a renamed
target or a dropped source, and the test fails, in CI and in WSL alike. The second
half is the one worth having: before it, a renamed library would have turned a
whole file green in CI by never running it, because the files that skipped did so
regardless of machine. Three tests pin the rule on a temporary tree. The honest
figure for win11 aquamarine is now 382 of 906 passing with 524 skipping and none
failing, and a green run there is still no evidence about the C, C++ or Rust; it
is evidence that the Python half and every check that needs no compiler hold.
**The one place the suite runs in full is WSL on win11 skyhorizon**, where every
library, every filter and every Rust binary exists: on Friday 16 October 2026 it
read 368 passed and nothing skipped there, and the fail-on-drift branch was seen
to fire by moving `libframe.so` aside for one run. The same rule now covers every
loader in the suite, the arithmetic libraries, the parity C libraries, the filters
and the Rust binaries alike, the Rust ones judged by `target/release` existing
rather than `build-host`, which the artefact's own directory supplies without a
second argument.

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

On the win11 skyhorizon demo laptop that disk is labelled `NOD_H7A3ZIQ` and
**the letter it takes is not fixed**, so the command resolves the volume by its
label and refuses when there is none:

    $v = Get-Volume | Where-Object { $_.FileSystemLabel -eq 'NOD_H7A3ZIQ' }; if (-not $v) { Write-Host 'NOT FLASHED: no volume labelled NOD_H7A3ZIQ, so the board is off, unplugged or still enumerating. The old image is still on it.' } else { Copy-Item build-fw\p09-codec.bin "$($v.DriveLetter):\" -Force; Write-Host "copied to $($v.DriveLetter):, the board is programming" }

**Why the letter moves, and the direction is backwards from the intuition.** It
was `D:` from Friday 2 October 2026 until Monday 19 October 2026, when it became
`E:` with nothing changed on the board or in its firmware. An external SSD had
been attached; Windows reports that enclosure as `Fixed`, so it keeps whichever
letter it was given, and the probe's disk is the **removable** volume, which takes
the lowest free letter. So the board is the one that moves, not the drive that was
added.

**What a copy to the bare letter costs.** It writes the image into the root of
whatever now holds that letter, reports success, flashes nothing, and the console
afterwards prints the **previous** image's banner. Every figure in that capture is
then attributed to the wrong image, and nothing in the output looks wrong, because
each image in this volume names itself and the name would be correct. Four
hand-overs on Monday 19 October 2026 named the letter before this was understood,
and none of them ran. The guarded build-and-flash line later in this file resolves
the volume the same way, which is where the form above comes from.

`probe-rs` and `openocd` are both reasonable tools for this part and neither has
been used in this repository. Chapters referred to `probe-rs run` until Saturday 3
October 2026; that was written from convention, never run here, and has been
replaced by the copy above.

### The programmer, which reports what it did, and the one option it needs

The copy above is silent by design, and that matters more than it sounds. The
drive's idle state is `DETAILS.TXT` and `MBED.HTM` alone, so a vanished `.bin`
with no `FAIL.TXT` is the normal picture after a successful program **and also
after nothing happened**. That state cannot tell a programmed part from a silent
one.

STM32CubeIDE bundles `STM32_Programmer_CLI.exe`, which reports every stage. On
the win11 skyhorizon demo laptop it is at

    C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.500.202603051304\tools\bin\STM32_Programmer_CLI.exe

**It needs `mode=UR` on this board and the default fails.** Established
Wednesday 7 October 2026. Without it, with the probe otherwise healthy, it
reports:

    Error: Unable to read device id from ROM table
    Error: ST-LINK error (DEV_TARGET_RESET_ERR)

twice, including its own automatic retry at 8 MHz. That reads like a dead target
and is not one. The default connect mode attaches to a **running** target;
`mode=UR` holds the reset line low while attaching instead. So the recipe is

    -c port=SWD mode=UR -w build-fw\<target>.bin 0x08000000 -v

**What the failure does and does not tell you**, because the distinction saved a
power cycle. In that state the probe still reports its serial number, its
firmware version, the board name and a measured supply voltage, so USB, the
probe and board power are all known good and the fault is only the debug port
attach. The voltage is the probe measuring the board's rail; it is **not**
evidence that the core is executing.

**Add `-rst` only when nothing needs to read the console.** An image that prints
once and stops is over in well under a second, so a reset at programming time
sends the whole report before a console can be opened. Leave `-rst` off, then
open the console, then press the black RESET button.

### What the attach reports, which is four facts from the part itself

Read Wednesday 7 October 2026 and worth having, because each was previously
known only from a machine-readable source rather than from this silicon:

    Device ID    0x480
    Revision ID  Rev X
    Device name  STM32H7A/B
    NVM size     2 MBytes
    Device CPU   Cortex-M7
    BL Version   0x92

The NVM size confirms the 2048 kB in `c/ld/stm32h7a3zi.ld` from the hardware
rather than from a device tree. The revision is worth one note so it is not
mistaken for a new question: ST's converter code has a
`HAL_GetREVID() <= REV_ID_Y` branch, and it sits in the `#else` that this part
never compiles, so **there is no revision fork for the H7A3** and reading Rev X
confirms that rather than complicating it.

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
| `p01-pll280`            | 1       | runs on the board. The raise to 280 MHz in eight gated steps, then the clock measured against the 32.768 kHz crystal, then the frequency counter and the signal source. This is the image every measured frequency in the volume comes from. It was missing from this table from Sunday 4 October 2026 until Tuesday 6 October 2026, which is what `python/tests/test_target_table_is_complete.py` now exists to prevent |
| `p02-ring-<mode>`, four of them | 2 | runs on the board |
| `p06-sampling-<backend>`, three | 6 | BUILT AND LINKED for the first time Tuesday 6 October 2026, 9344 to 9476 bytes of FLASH each, with `-DCMSIS_DEVICE_DIR=<pack>/Drivers/CMSIS/Device/ST/STM32H7xx/Include` on win11 skyhorizon; nothing flashed. Still has no independent build check and no CMSIS pack in CI, so one toolchain on one machine is all that has compiled them, and until that first build they were missing `c/instr/lseref.c` and could not have linked at all |
| `p09-codec`             | 9       | runs on the board |
| `p09-codec-pad0`, `p09-codec-pad16`, `p09-codec-pad32`, `p09-codec-pad48` | 9 | the same image displaced by 0, 16, 32 and 48 bytes, for the placement experiment. The pads are whole multiples of the 16 byte instrument resolution and not of the 32 byte cache line, so the experiment is able to fail |

Everything else in the volume is not built. Where such a chapter shows a command,
read it as a plan.

## The flash size of every target, as a baseline

Recorded Friday 9 October 2026 from one build on win11 skyhorizon with CubeIDE's
`arm-none-eabi-gcc` 14.3.1, **so that the next change to a shared file can be
checked rather than trusted.** `c/board/startup.c`, `c/board/system.c`,
`c/board/retarget.c` and `c/ld/stm32h7a3zi.ld` are linked into every image here, and
until now there was no recorded figure to compare against after touching one.

| target | FLASH, Friday 9 October 2026 morning | after `marker_toggle`, same day | after the section fix, Friday 16 October 2026 | after the TIM6 factoring and the transfer engine, Monday 19 October 2026  after the hardware marker on TIM3, Sunday 25 October 2026 |
|---|---|---|---|---|---|
| `p01-first-light` | 11 772 B | not rebuilt | not rebuilt | not rebuilt | not rebuilt |
| `p01-pll280` | 36 772 B | not rebuilt | not rebuilt | not rebuilt | not rebuilt |
| `p02-ring-none` | 9980 B | not rebuilt | not rebuilt | not rebuilt | not rebuilt |
| `p09-codec` | 10 608 B | not rebuilt | not rebuilt | not rebuilt | not rebuilt |
| `p06-sampling-timer` | 15 196 B | **15 220 B**, +24 | not rebuilt | **15 276 B**, +56 | **15 576 B**, +300 |
| `p06-sampling-systick` | 10 232 B | **14 420 B**, +4188: +4164 when it reached the converter later that morning, then +24 | not rebuilt | **14 420 B**, unchanged | **14 420 B**, unchanged again |
| `p06-sampling-dma` | 10 248 B | not rebuilt | **9992 B**, -256, then **10 076 B**, +84 | **15 812 B**, +5736 | **16 344 B**, +532 against Monday 19 October 2026 and NOT attributable |

**AND THE TRANSFER-ENGINE BUILD IS 16 648 B**, which is +304 on its figure of Monday 19 October 2026: +300 for the pad read-back it shares with the timer build and +4 for the compare value. **It is the first image of that build whose marker the witness has ever seen**, and it produced 60 164 edges in sixty seconds where the same build drove nothing at all on Tuesday 20 October 2026.

**AND 15 880 B LATER THE SAME DAY**, four bytes more, which is the compare value moving off zero: one immediate operand became a division of a variable. That is the image whose marker finally reached the pin, and the four bytes are the whole difference between a silent pin and 60 175 edges in sixty seconds.

**AND THE TIMER BUILD IS 15 876 B SINCE Monday 26 October 2026**, which is +300
again, for the two `printf` calls that read back the marker pin's mode and its
alternate function number and the format strings behind them. No prediction was
published for that figure either, and this time the reason is better: the change
was written in the middle of a diagnosis, between a witness run that measured the
mains and the next one. **A size read afterwards is still a figure rather than
evidence**, and it is recorded here because the captures are checked against this
table and a capture of an unpublished size is refused by `test_console_captures.py`.

**The fifth column, Sunday 25 October 2026, and one of its three figures cannot be
attributed, which is a cost of something not recorded five days earlier.**

**No size prediction was published before this build, and the practice says one
should have been.** The fourth column's three predictions were written down first and
all three held, which is what made that column worth having. This column was read
afterwards. The omission is recorded rather than quietly skipped, because a figure
read afterwards is weaker evidence than the same figure predicted, and the difference
is exactly what the fourth column demonstrated.

**The tick build is unchanged at 14 420 bytes, and that is the strongest of the
three.** It links `marker.c` with a second init it never calls and `tim3_bringup.c`
with two functions it never calls, so `-ffunction-sections` with `--gc-sections` must
discard all three. It did, to the byte, for the second time on this row. **A discard
demonstrated twice on different code is a property of the build rather than a
coincidence.**

**The timer build grew 300 bytes** for four register writes, a report function, two
read-back lines and their format strings, plus the alternate-function init. That is
plausible and it is not predicted, so it is a measurement of nothing in particular.

**And the transfer-engine build's +532 is against the wrong baseline.** Its Monday
19 October 2026 figure of 15 812 bytes predates the overrun work of Tuesday
20 October 2026, which added `OVRIE`, a handler, a counter and a report line to that
build. So the delta spans two changes and cannot be split between them from this
table.

**Why the baseline is missing is worth more than the number.** The image that ran on
Tuesday 20 October 2026 printed its size in its banner, and the only capture of that
banner was a truncated seven-line file I declined to commit on the ground that a
banner with no data proves nothing. **That was too strong: it was the only record of
that image's size.** A banner is a measurement of the image even when the run that
follows it is lost, and the rule it should have been judged by is the one this volume
already keeps, that the banner is how an image identifies itself. The capture is gone
and the figure with it.

**The fourth column was predicted in writing before the build ran and all three
predictions held**, which is the first time this table has been used that way rather
than read afterwards. The timer build was predicted to grow slightly, because the
moved code sits in its own translation unit and can no longer be inlined into
`acq_start`: +56 bytes. The tick build was predicted not to move at all, because it
links `tim6_bringup.c` and calls nothing from it, so `-ffunction-sections` with
`--gc-sections` should discard both functions: unchanged at 14 420 B, and that is the
discard demonstrated rather than assumed. The transfer-engine build was predicted to
grow substantially, having gained the whole configuration with eight read-back reports
and their format strings: +5736 bytes.

**And its map confirms the buffer again**: `.dma_buffers` holds 256 bytes at
`0x24000000`, which is AXI SRAM and reachable by the transfer engines, the placement
whose one-letter error on Friday 16 October 2026 is what `check_section_placement.py`
exists for.

**The third column refuted what the second column predicted for that row, which is
why the prediction was written down.** The second column said a rebuild of
`p06-sampling-dma` was expected to move it by 0, because it calls `marker_init` and
nothing else in `marker.c`. It moved by **-256**, which is exactly the size of the
sample buffer, and the reason is the section fix of Friday 16 October 2026 rather
than the marker: that buffer's section attribute named a section no linker rule
collected, and **a section attribute on uninitialised data makes the compiler emit a
LOADED section of zeros**, since nothing tells it the section is `NOLOAD`. So the
orphan cost 256 bytes of image. Collected into `.dma_buffers (NOLOAD)` it costs none,
and `arm-none-eabi-nm` now reports it as `b`, a zero-filled symbol, at 0x24000000.

**The +84 refuted a prediction and gave a better fact than the prediction had, and
the whole of it is one function.** `arm-none-eabi-nm` on the new image reports
`DMA1_Stream1_IRQHandler` as `T`, a real definition, with a size of exactly 84
bytes, so nothing came in beside it; the other handlers appear as `W`, weak aliases
sharing `Default_Handler`'s eight bytes at one address, which is the scheme
`startup.c` describes doing its work.
Adding the vector table entry for `DMA1_Stream1_IRQHandler` was predicted to
leave every image's figure untouched, because the table's extent is set by its
highest index and the new entry at 12 sits far below USART3 at 39. `.isr_vector`
did stay 224 bytes in all three images rebuilt, and `p01-first-light` and
`p06-sampling-timer` did not move. **The transfer-engine image grew 84 bytes**,
and the cause is that the build compiles with `-ffunction-sections` and links with
`-Wl,--gc-sections`: until the table referenced it, NOTHING referenced that
handler, so the linker had been discarding it and **it was not in the image at
all.** That is a stronger statement than the one the record made, which was that
the handler could never run: it was not there to run. The +84 is the handler's own
code arriving for the first time since it was written on Saturday 3 October 2026.

**What the old image's map file would have said, and it was not kept.** The figure
moving by exactly the buffer's size establishes that the orphan was loaded; whether
its run-time address was in the FLASH region itself or in a RAM region with its load
image in FLASH cannot be recovered from the figure alone, and that build's map file
no longer exists. **An inference that it had been in `.bss` in DTCM was stated in
chat before this arithmetic was done, and it is withdrawn**: both possibilities are
wrong for a transfer engine's buffer, and which one it was is now unanswerable rather
than answered. Keeping the map file of an image whose placement is in question is
the lesson, and it costs one copy.

The second column was added the same day, Friday 9 October 2026, after `marker.c`
gained a toggle in place of a pulse: the two images that call it grew by the same
24 bytes, which is the static level, its exclusive-or and the select between the
two `BSRR` words. The four images that do not link `marker.c` were not rebuilt and
say so rather than carrying a figure nobody measured.

**Why this table exists rather than a sentence.** The linker script gained a
`PROVIDE` that day, and the claim made for it was that the four non-P06 images would
be unaffected because a `PROVIDE` defines a symbol only where it is referenced. That
claim could not be tested, because no figure had ever been written down. It is
testable from the next such change onward, which is the whole value of the table.

It is not a budget and nothing fails on it. The proper version is the size gate the
improvement plan describes, with a figure per target in a file CI reads; this is the
one-table version that costs nothing and removes the excuse.

## Reading the console

**THE BOARD IS SHARED, SO REFLASH IMMEDIATELY BEFORE EVERY CAPTURE.** Two projects
use this one NUCLEO-H7A3ZI-Q, so the image on it at any moment is whatever the last
session copied to the probe's disk, and that may not be yours. A capture taken
without reflashing first is a capture of an unknown image, and every number read out
of it inherits that doubt.

**Reflash and capture as consecutive steps with nothing between them**, and do not
reason from "I flashed this earlier". Earlier is not a guarantee when somebody else
has the same disk.

**Two things make a capture attributable after the fact**, and both are worth
building into a report rather than hoping for. The first is a line format no other
project prints, which is why P06's report line carries field names rather than bare
numbers. The second is a monotonic counter: a reflash resets the part, so the banner
reappears and the counter restarts, and a reader that counts banners and checks every
counter step detects the swap without being told. P06's `MEASUREMENT.md` requires
both, and the validity check it specifies for restarts covers this case unchanged.

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

**And that is what happened.** The push of Tuesday 6 October 2026 at 02:12 UTC
started three fresh runs, and all five jobs finished green in 14 to 46 seconds,
`firmware / cross-build` among them, which had not run since `0105031`. Because
nothing was reverted, the tree that built green contains everything from
`faedc31` and `0e2c748`, so the content those two commits added is now checked
even though their own runs never were.

Be careful what that shows. It shows the condition cleared. It does not show why
there were no runners, and one recovery is not a mechanism any more than one
cancellation was. What can be said is bounded and useful: the runs stop at
`15m 2s` when no runner arrives, nothing in this repository produces that, and
pushing again is the cheap thing to try first.

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
