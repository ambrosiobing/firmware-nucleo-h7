# P20: Three jobs at once: the node as an RTOS application

Status: c=host cpp=none python=host rust=host

**Started on the host, Tuesday 27 October 2026, and it is the part of chapter 20
that decides the chapter without a kernel.** Criterion 3 is that "a million
release events are recorded with the timer source, and the report gives the
median, the 99.9th percentile and the maximum, with the overflow count shown so
the tail can be trusted", and criterion 4 is that "all three latency figures
meet the budget written in the budget table above". The figures come from the
board. **The arithmetic that turns them into a verdict does not**, and it is
written, pinned and proven against a twin on a laptop with no board, no FreeRTOS
and no shield.

**What it is.** P08 to P12 rebuilt as a statically allocated task set, with a
latency distribution over a million events against a deadline stated in advance.

## What runs

| Piece | What proves it |
|---|---|
| `c/latstat.c`, the histogram, the ranks, the verdict and the stack arithmetic | 7 recorded runs, 2 built histograms, 4 refusals, 4 stack rows, 2 stack refusals |
| `python/latstat.py`, the twin, standard library only | the same cases, and the two readings the chapter's wording invites kept unused |
| `rust/src/lib.rs`, `no_std`, and `rust/src/main.rs`, the filter | the same cases through a four verb protocol, and six unit tests |
| `vectors.json`, the oracle | **compared exactly, with no tolerance anywhere**, because all of it is integer |

**The instrument is step 5's**, unchanged: 1024 bins, one microsecond per bin, a
running maximum and an overflow counter. **The budgets are table 20.4's**,
quoted and not chosen: a median of 100 microseconds, a 99.9th percentile of 250
and a maximum of 1 millisecond, all written before the first run.

**The rank definition is pinned, because the chapter states none.** A percentile
is the sample of rank ceil(p times n), counting from one. Two reasonable choices
differ by one sample, and an equivalence test between two implementations finds
that difference immediately.

## A percentile from a histogram is a bin, not a value

**This is the first finding and it changes the verdict's shape.** Bin k holds
every sample whose microsecond value truncated to a whole number is k, so the
true values in it run from k up to just under k plus one. Table 20.4's rows are
whole microseconds. The median budget is 100 and the bin width is 1, so:

| A median that lands in bin 100, against a budget of 100 microseconds | |
|---|---|
| the two-way comparison criterion 4's wording invites | **pass** |
| this project | **undecided**, because part of that bin does not meet the budget |

So there are **three verdicts and not two**. A pass needs the whole bin below
the budget, a failure needs the whole bin above it, and a bin sitting on its
budget decides nothing. The third verdict is the honest answer rather than a
missing feature.

**And the error at the boundary is always a pass.** Step 5 computes microseconds
as cycles divided by cycles per microsecond, which truncates toward zero, so
every sample is recorded at or below its true value by up to one microsecond.
Two runs make that concrete, and the oracle carries both:

| Run, at 280 cycles per microsecond | Recorded median | Every sample |
|---|---|---|
| 28000 cycles, exactly 100 microseconds | 100 | sits on the budget |
| 28252 cycles, 100 and nine tenths | **also 100** | **exceeds the budget** |

The two are indistinguishable in the report. A two-way comparison passes both.

## The overflow count changes a percentile's type and not only its tail

The chapter keeps the overflow counter "so the tail can be trusted" and then
reads its percentiles from the histogram. **If a percentile's rank falls inside
the overflow count there is no bin to report.** Two releases of 2000
microseconds in a thousand put the 99.9th percentile's rank of 999 past every
bin; one release in a thousand leaves it in bin 50. So each percentile carries a
flag saying whether the histogram could answer, and an unresolved percentile is
reported as unresolved rather than as the top bin. Reporting 1023 for a sample
that is at least 1024 is the one wrong answer nothing downstream can detect.

An unresolved percentile still decides the verdict where it can: at least 1024
microseconds fails any budget below 1024, which is where all three of table
20.4's rows sit.

## The rank arithmetic overflows 32 bits long before the counter does

The 99.9th percentile's rank is the ceiling of 999 times the count over 1000.

| Count | 999 times the count | In 32 bits |
|---|---|---|
| a million, the chapter's run | 999000000 | fits |
| four thousand million, which the counter holds | 3996000000000 | **wraps to 1680414720** |

At four thousand million samples the correct rank is 3996000000 and lands in bin
900; the wrapped rank lands in the first populated bin and reports a percentile
far below the truth, which passes a budget row the right answer fails. The
product is 64-bit in the C, the oracle carries a histogram with that count, and
the twin has no width at all, so this is exactly the divergence an equivalence
test exists to catch.

## The kernel reports free stack and the budget table wants used

Criterion 6 is that "every task's stack high-water mark is reported in the same
result line, and no task has used more than eighty percent of its budget".
`uxTaskGetStackHighWaterMark` returns the **smallest free space** the task has
ever had.

| A task with a 1024 byte budget whose kernel figure is 205 | |
|---|---|
| used, which is what criterion 6 asks about | **819 bytes, 799 per thousand** |
| the kernel's figure read as used | 205 bytes, 200 per thousand |

Reading the kernel's number as the used number turns a task at eighty percent
into a task at twenty, and **the direction of that error hides the problem
rather than inventing one**. A free figure larger than the budget is refused
rather than clamped, because it means the two numbers came from different tasks.

The eighty percent rule is a comparison and not a division, so it is exact at
the boundary: 819 bytes of 1024 is allowed and 820 is refused.

## And clippy found a sixth finding, which the gate had not reached

Finding one says a pass needs the whole bin below its budget. That was written
as "bin plus one at or below the budget", which is the same thing in arithmetic
and **not** the same thing in a `uint32_t`. The maximum passes through that
comparison unbounded by the bin count, and `lat_record` produces 4294967295
microseconds from a cycle count of 4294967295 at one cycle per microsecond:

| A maximum of 4294967295 microseconds against the 1 ms budget | |
|---|---|
| C, before Wednesday 28 October 2026 | the sum wrapped to 0, which is at or below 1000, so **PASS** |
| Python, before Wednesday 28 October 2026 | arbitrary precision, so **FAIL** |
| all three, since Wednesday 28 October 2026 | a strict comparison, which cannot wrap, so **FAIL** |

**So the worst run this instrument can record was reported as meeting the
budget**, the two implementations disagreed about it, and no oracle case had
reached the corner. It was found by clippy on the Rust twin, which rejects
`bin + 1 <= budget` as `int_plus_one`. The oracle now carries that run and a
test prints what the wrapped form returned.

That is the second time this volume's equivalence gate has been extended by a
corner a third implementation turned up rather than by the gate failing, after
P16's root mean square on Tuesday 27 October 2026.

## The same defect behaves differently in Rust, which is the second instance

Finding four is a 32-bit product wrapping. The C wraps silently in every build.
The Rust panics under test and wraps in this workspace's release profile,
because cargo's dev profile takes `overflow-checks` from `debug-assertions`
while the root manifest's release profile does not set it. **So the claim is
that Rust's checks would have caught this during development and not in the
field**, which is smaller than "this cannot happen in Rust" and is what the two
profiles support.

**P17 found the same shape on Tuesday 27 October 2026** for its subtraction. One
instance was an anecdote about a profile and two make it a property of the
toolchain. In both cases the fix was available in every language and the C took
it: the product is 64-bit in all three implementations here.
[rust/README.md](rust/README.md) has the table.

## What is not here

FreeRTOS, the four tasks and their fixed priorities, the static allocation, the
notification and semaphore wakes, the mutex and what its priority inheritance
does not do, the supervisor and its watchdog, the second kernel, the
active-object comparison, and **every latency, stack and processor load
figure**. Those need the board, the IKS4A1 shield and a seventeen minute run.

**There is no measured number in this project.** The budgets are quoted from
table 20.4, which was written before any measurement, and the verdict function
is what will judge the figures when they arrive.

**What has to come first.** P01 owns the cross toolchain file, the startup code
and the linker script, so nothing here can be flashed until that project exists,
which it now does. The kernel itself is the next step and it is a download
rather than a question.

Chapter 20 of [the book][book] is the written design. When the board half starts
here it follows that chapter's acceptance criteria, and anything the chapter
leaves as "not measured" is either measured or still says so.

[book]: ../../chapters/20-three-jobs-at-once.md
