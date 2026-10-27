# P20 in C

**State: host, since Tuesday 27 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `latstat.h` | the instrument, the three budget rows, six refusals, and the five findings |
| `latstat.c` | the histogram, the ranks in 64 bits, the three-way verdict, the stack arithmetic |

**What is here is the deciding arithmetic and not the kernel.** Chapter 20's
criteria 3, 4 and 6 are about numbers the board produces and a judgement made
from them. The judgement is the part that can be wrong in a way no run would
reveal, so it is written first, pinned, and compared against the Python twin on
every build.

Four functions:

| | |
|---|---|
| `lat_record` | one sample, with the truncating division step 5 prints |
| `lat_report` | the median bin, the 99.9th percentile's bin, the maximum, the overflow, and a flag per percentile |
| `lat_verdict` | the three budget rows judged as pass, fail or **undecided** |
| `lat_stack` | one task's used bytes from the budget and the kernel's free figure |

**The widths are the subject and not an accident.** The rank of the 99.9th
percentile is the ceiling of 999 times the count over 1000, and 999 times a
count near four thousand million overflows a 32-bit product. The product is
`uint64_t` here, the bin walk accumulates in `uint64_t`, and the oracle carries
a histogram with that count so the choice is tested rather than asserted. The
Python twin has no width at all, which is why the comparison is the proof.

**The count is checked against the histogram rather than trusted.** `lat_report`
sums the bins and the overflow and refuses when the total disagrees with the
counter, because a counter written separately gives ranks that point at the
wrong samples and nothing in the printed result would show it. That refusal is
also what makes the oracle's two built histograms legitimate inputs rather than
a back door.

**Six refusals, each for a reading that would otherwise pass as a figure:** a
null pointer; zero cycles per microsecond; a sample counter already at its
maximum, where step 5's own `hist_n++` would wrap and every rank below it would
be wrong; no samples at all, where a report of zero microseconds would read as
the fastest run ever measured; a count that disagrees with the histogram; and a
stack pair where the free figure exceeds the budget, which means the two numbers
came from different tasks.

The five findings are in the header and in [../README.md](../README.md). What is
not here, and what it waits on, is in the same place.
