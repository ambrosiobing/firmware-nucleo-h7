# The console captures, as they came off the port

Twelve files, taken on Friday 9 October 2026, Friday 16 October 2026, Saturday
17 October 2026 and Monday 19 October 2026. They are the board's own words, unedited,
including the timestamps and the truncation.

Ten were committed on Monday 19 October 2026 because P02's page had come to say a
reader could not check its figures against the board. **The last two were committed
the same day for the opposite reason**: they are the evidence that the TIM6 factoring
changed nothing, and a verdict without its capture is the thing those ten exist to
stop.

**Why they are here.** Until this directory existed, every board figure in this
volume was transcribed prose: the number appeared in a README and the capture it came
from lived in somebody's scrollback. P02's page had come to say that in so many
words, that a reader could not check its figures against the board, which is a strange
thing for a volume whose subject is the difference between a claim and a measurement.
Each of these carries its own build identity in the banner, the image size in bytes
and the build stamp, so by this volume's own test a capture with its identity **is**
evidence. `.gitignore` says which kind of capture stays out and why.

## What each one is

| File | Back end | Image | Bytes | Lines | Banners | Last `seq` | Line stamps |
|---|---|---|---|---|---|---|---|
| `console-systick-20261009-160503.txt` | systick | 14 420 B | 13 075 | 162 | 1 | 1995 | yes |
| `console-systick-20261009-160856.txt` | systick | 14 420 B | 13 157 | 163 | 1 | 2010 | yes |
| `console-timer-20261009-141632.txt` | timer | 15 220 B | 8 397 | 105 | 1 | 1065 | yes |
| `console-timer-20261009-151717.txt` | timer | 15 220 B | 56 675 | 701 | 1 | 10005 | yes |
| `console-timer-20261016-212342.txt` | timer | 15 220 B | 56 675 | 701 | 1 | 10005 | yes |
| `console-timer-20261016-214100.txt` | timer | 15 220 B | 56 592 | 700 | 1 | 9990 | yes |
| `p06-timer-console-2026-10-17-0809.txt` | timer | 15 220 B | 6 384 | 112 | 1 | 1140 | no |
| `p06-timer-console-2026-10-17-0810.txt` | timer | 15 220 B | 12 853 | 219 | 5 | 840 | no |
| `p06-timer-console-2026-10-17-0852.txt` | timer | 15 220 B | 6 292 | 112 | 1 | 1140 | no |
| `p06-timer-console-2026-10-17-0857.txt` | timer | 15 220 B | 8 199 | 145 | 2 | 1050 | no |
| `p06-timer-postmove-2026-10-19-1411.txt` | timer | **15 276 B** | 6 392 | 112 | 1 | 1140 | no |
| `p06-timer-postmove-2026-10-19-1419.txt` | timer | **15 276 B** | 6 211 | 113 | 1 | 1155 | no |

`python/tests/test_console_captures.py` reads every one of these and checks the
columns above against the files, so this table cannot drift from them.

## What the last two are for

`p06-timer-postmove-2026-10-19-1411.txt` is the capture that met the TIM6 move's
published criterion: all twenty-four configuration read-back lines and the timer
clock line identical to `console-timer-20261016-214100.txt`, hex for hex, across a
code change, a toolchain reinstall and a drive-letter shift. The image line reads
15 276 B where that one reads 15 220 B, which the criterion excluded in advance
because the moved code can no longer be inlined.

Its `mean` column is the other half of the story and looks alarming: 9364 rising to
10323, down to a band near 3600 to 5899, back up, then settling at 3052 to 3409. On a
floating PC3 that is pickup, and `p06-timer-postmove-2026-10-19-1419.txt` is what
settles it: the same image with PC3 grounded at CN11 pin 37 reads `mean` **2 to 6**
across 77 reports, against the threshold of within a few counts of zero published on
Thursday 8 October 2026. A converter sampling wrongly does not return to a few counts
of zero when its pin is tied to ground, so the move is excluded as the cause of the
swing by measurement rather than by argument.

**Both report the same image size, and that is what makes them a pair.** The
verification and its control were taken from one build, twenty-three minutes apart,
with nothing rebuilt between them.

## Three things that are only visible with the whole set side by side

**The build stamp cannot tell the back ends apart.** All ten read `Oct  9 2026
09:31:08`, including the 14 420-byte systick image and the 15 220-byte timer image,
**and so do the two post-move captures**, whose image is 56 bytes larger. `main.c` was
not recompiled by the factoring, so its stamp did not move even though the image did,
which is the same fact a third time and the strongest form of it yet.
`main.c` is shared by all three back ends, so its stamp is identical across images
that differ by 800 bytes and by which mechanism moves the samples. Only the printed
image size separates them. On Sunday 18 October 2026 this volume recorded that the
stamp cannot see a change in `startup.c`; this is the stronger version of the same
fact, and it is the reason `--image-bytes` is a required argument to `scan.py`.

**Twelve report `calibration took 180 polls`**, across four separate days, and
**the thirteenth reports 187**, on Sunday 25 October 2026, the first hardware-marker
run.

**THAT CLOSES A GAP THIS FILE OPENED ON Monday 19 October 2026.** The paragraph here
said this volume's prose cited a 187 reading, that 187 was in none of the captures,
and that the claim therefore rested on a session whose console was never saved: not
refuted, merely unsupported by anything a reader could open. **It is supported now.**
The figure that had only a memory behind it has a file behind it, and the claim that
the count moves between sessions is a measurement rather than a recollection.

**THIS FILE SAID IT WAS NOT AN EFFECT OF THE CHANGE AND THAT IS WITHDRAWN, Monday
26 October 2026.** The second TIM3 image reported 187 as well, so the tally is **two
of two images carrying the TIM3 marker at 187 and twelve of twelve without it at
180**, across four days and three images on the old side. The split falls on the
change, not on the session.

**The count is a software loop, which is why this is plausible rather than
coincidental.** It counts iterations spent waiting for `ADCAL` to clear, so it
measures the loop's own speed against a fixed hardware interval, and code placed in
front of it moves alignment, flash prefetch and caching. 3.9 per cent is the size of
such an effect. **No mechanism is claimed**: one correlation with a plausible cause
is not a measurement of it, and the experiment that would settle it is to move the
code layout without touching the converter and see which the count follows.

**And it moved a test from a coincidence to a set.** `test_console_captures.py`
asserted that every capture reports 180, which was true of twelve and false of the
thirteenth. The quantity never allowed that assertion: it is measured and the
converter's bring-up says so. The test now pins the values that have been SEEN, so a
third value is a deliberate entry with its session beside it.

**Six carry a per-line timestamp and seven do not**, which was "six and four" in
this file until Sunday 25 October 2026 and had been wrong by two since Monday
19 October 2026: the count was not updated when the two post-move captures went in.
It is now checked by counting rather than by recollection. The six from Friday
9 October 2026 and Friday 16 October 2026 were taken with a reader that stamps each
line using the host clock.
The four from Saturday 17 October 2026, the two from Monday 19 October 2026 and the
one from Sunday 25 October 2026 were taken with a one-line reflash-and-console
command that does not. This matters for exactly one thing: `MEASUREMENT.md`'s second
route to criterion 1 measures the mean rate from the host clock and **requires**
stamped lines. So those four support the bring-up readings, the overrun mode and the
poll counts, and they cannot support that method.

## One coincidence that was checked rather than assumed

Two files are exactly 56 675 bytes, a week apart. That is the shape of a duplicate,
so it was checked before this directory was described: the two differ in all 700 of
their content lines and their checksums differ. Both runs emitted 701 lines of the
same shapes and reached the same `seq 10005`, and every line carries a fixed-width
timestamp, so equal totals are what identical run shapes produce. A test pins this,
because the next person to notice those two numbers will have the same suspicion.

## What a capture is evidence of, and what it is not

**It is evidence of** what that image printed on that date: the eight converter
bring-up steps with what was written and what read back, the timer clock the firmware
derived, the overrun mode as found, the poll counts, and the running block and mean
figures.

**It is not evidence of** the sampling rate. Every criterion in `MEASUREMENT.md`
except criterion 1 is about the instants at which conversions happened, and nothing on
the die can observe those: the only instrument that can is the MCC 118 witness on the
marker pin. A console capture cannot refute or support criteria 2, 3 or 4, and the
file that says so says it from before the first run.

**And it is not the witness's data.** Sixty seconds at 100 kS/s is six million
samples, written as CSV and npz on the Raspberry Pi into a `captures/` directory that
`.gitignore` refuses by name. That rule is about those files and not about these.

## How another one is taken

One PowerShell line on win11 skyhorizon that copies the `.bin` to the probe's disk and
opens COM13 at 115200 in the same command, because this board is shared with another
project and a console opened after the copy misses the banner. `docs/building.md`
carries the flashing route and `projects/P06-timer-sampling/README.md` the reflash
rule. If the capture is meant to support criterion 1 by the host clock, the reader
must stamp each line, which the four files of Saturday 17 October 2026 did not.
