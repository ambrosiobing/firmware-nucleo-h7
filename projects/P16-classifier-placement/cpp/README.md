# P16 in C++

**State: host, since Wednesday 28 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `winfeat.hpp` | header only: the three pinned feature groups, the bounded tree walk, and a fourth route to the integer square root |
| `winfeat_filter.cpp` | two verbs on stdin, so one parity test drives this beside the Rust |

## What C++ adds here, and this time it is not nothing

The answer varies by project in this volume and is worth keeping accurate. There
are now four answers on record:

| | What C++ adds |
|---|---|
| P18 | one function template where the C needs two forty line copies, which is a real gain and the first in the volume |
| P12 | the C's lack of an allocator costs it sixty lines of fixed buffers, bounds checks and an insertion sort the other three do not have |
| P17 | almost nothing: four small functions over fixed-width integers, which C expresses exactly as well |
| P16 | a **fourth independent algorithm** for the one quantity in the project where implementations have real room to disagree |

Criterion 1 is an equivalence gate, and **a gate is worth running in proportion
to how independently its implementations were written**. The floored integer
square root is where that independence can actually be bought, so the four
implementations now reach it four ways:

| | The floored integer square root |
|---|---|
| C | a bit-by-bit descent, written out |
| Python | `math.isqrt` |
| Rust | `u64::isqrt` |
| C++ | `std::sqrt` followed by a correction, which is the route the C header refuses by name |

It has already paid once. On Tuesday 27 October 2026 the Rust found a corner
where the C and the Python disagreed: a window of negative full scale, where the
root is exactly 32768 and Q15 stops one short. All four saturate at 32767 now.

## What this file found, which corrects the C header's reason

`../c/winfeat.c` justifies its descent in one sentence: "A float sqrt followed by
a cast would agree almost always, and almost always is what an exact gate exists
to refuse." That is correct about sqrt and cast, and it is **not** an argument
against floating point as such, because a sqrt followed by a correction is exact
for every input:

```cpp
r = (uint32_t) std::sqrt((double) v);   // close, and not trusted
while (r * r > v)             { --r; }  // too high
while ((r + 1) * (r + 1) <= v){ ++r; }  // too low
```

So the question becomes how often the correction is needed, and **inside this
project's range the answer is never**. The mean of the squares cannot exceed
1073741824, which is two to the thirtieth. Across 98303 adversarial inputs in
that range, every k squared less one, k squared and k squared plus one for every
k up to 32768 plus the endpoints, the uncorrected cast was already exact every
single time.

**The place where sqrt and cast fails has a shape, and it is far above this
project.** It is v equal to k squared less one, where the true root sits a hair
below a whole number and the double rounds up to it. That needs the hair to be
narrower than half a unit of least precision, which first happens at k equal to
two to the twenty-sixth:

| | |
|---|---|
| smallest failing input | 4503599761588224, which is 67108865 squared less one |
| the cast gives | 67108865 |
| the answer is | 67108864 |
| this project's largest root | 32768, which is two to the fifteenth |

Eleven binary orders of magnitude. **So the C header's sentence is right as a
rule about a function typed `uint64_t` and wrong as a statement about this
window's arithmetic**, and both halves are worth having. The correction loops
stay, because the type says `uint64_t` and the next caller may not be this
project. Two tests in `python/tests/test_winfeat_parity.py` run the claim rather
than repeating it: one asserts no correction is needed across the project's
range, the other asserts one *is* needed at 67108865 squared less one, so the
loops are shown to be reachable rather than assumed to be.

A clamp covers the third branch. The sqrt of the largest `uint64_t` rounds up to
two to the thirty-second, and without the clamp `(r + 1) * (r + 1)` wraps to
zero, compares at or below any `v`, and walks the counter upward for ever. That
defect was in the first draft of this file and was found by reading it rather
than by running it, since nothing in this project reaches that input.

## What else the language gives, which is the usual short list

`std::optional<Error>` makes a refusal a different type rather than a negative
return code, and `std::array` fixes the window without a macro. That is the
whole of it.

## What is identical, and what is not

Identical: the window of 128 samples and three axes, refused rather than scaled;
the mean floored toward negative infinity; the mean of the squares floored before
its root is taken; the saturation of the root mean square and of peak to peak at
32767; the six refusals and their names; the depth bound; and at or below the
threshold going left.

Not identical, by language: the mean is written as a division and a correction
here rather than as a shift, because a right shift of a negative signed value is
implementation defined in C and C++ while Rust's is defined as arithmetic. The
same floored answer, reached by the spelling each language can guarantee.

**The band energies of step 3 are absent here too**, for the reason
[../c/winfeat.h](../c/winfeat.h) gives: a quantity named as coming from an
unspecified transform cannot enter an exact gate.

## The filter protocol, and one build flag worth naming

Two verbs, counts before data, the same ones `../rust/src/main.rs` speaks, so one
parity test drives both.

```text
F <n> <axes> <n*axes samples>
    -> mean=a,b,c rms=a,b,c p2p=a,b,c vector=v0,..,v8
C <nodes> <depth_max> <nfeat> <feature leaf threshold left right, per node> <nfeat features>
    -> class=k
```

The build passes `-ffp-contract=off`, as every floating-point host build in this
repository does. **The test that enforces that flag could not have required it
here.** It reads the source named in `python/tools/build_host.py` and looks for
the words `float` and `double`; this is the first header-only library registered
there, the `.cpp` names no floating point at all, and every `double` is in
`winfeat.hpp`, which the test never opened. The flag was passed because the
author knew, which is exactly the condition the test exists to remove. It now
follows a local include, one level, and names this file as floating point
through its header.
