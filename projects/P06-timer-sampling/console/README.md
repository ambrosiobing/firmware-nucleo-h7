# The console captures, as they came off the port

Ten files, committed on Monday 19 October 2026, taken on Friday 9 October 2026,
Friday 16 October 2026 and Saturday 17 October 2026. They are the board's own words,
unedited, including the timestamps and the truncation.

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

`python/tests/test_console_captures.py` reads every one of these and checks the
columns above against the files, so this table cannot drift from them.

## Three things that are only visible with all ten side by side

**The build stamp cannot tell the back ends apart.** All ten read `Oct  9 2026
09:31:08`, including the 14 420-byte systick image and the 15 220-byte timer image.
`main.c` is shared by all three back ends, so its stamp is identical across images
that differ by 800 bytes and by which mechanism moves the samples. Only the printed
image size separates them. On Sunday 18 October 2026 this volume recorded that the
stamp cannot see a change in `startup.c`; this is the stronger version of the same
fact, and it is the reason `--image-bytes` is a required argument to `scan.py`.

**All ten report `calibration took 180 polls`**, on three separate days a week apart.
This volume's prose says that count is known to move between sessions, on the strength
of a 187 reading, and **187 is in none of these captures**. So that claim rests on a
session whose console was never saved. It is not contradicted here, because a figure
seen once and not kept is not refuted by ten later figures agreeing with each other;
it is simply no longer supported by anything a reader can open.

**Six carry a per-line timestamp and four do not.** The six from Friday 9 October
2026 and Friday 16 October 2026 were taken with a reader that stamps each line using
the host clock.
The four from Saturday 17 October 2026 were taken with a one-line reflash-and-console
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
