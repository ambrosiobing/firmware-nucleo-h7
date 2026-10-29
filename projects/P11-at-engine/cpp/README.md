# P11 in C++

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `atparse.hpp` | header only: the classifier, the wrap-safe deadline test, the backoff |
| `atparse_filter.cpp` | three verbs on stdin, so one parity test drives this beside the Rust |

The reasoning this project is built on is in [../c/atparse.h](../c/atparse.h)
and is not repeated here, including the one that decides the project: chapter
11's deadline test is a signed difference on a 32 bit counter, the obvious form
is correct for forty-nine days and then is not, and **a board cannot demonstrate
that without waiting forty-nine days**.

## What C++ adds here is two C conventions becoming types

The volume's running list, kept accurate because the question is only worth
asking once per project if the answer can come back different:

| | What C++ adds |
|---|---|
| P18 | one function template where the C needs two forty line copies |
| P12 | the C's lack of an allocator costs it sixty lines of fixed buffers, bounds checks and an insertion sort |
| P17 | almost nothing: four small functions over fixed-width integers |
| P16 | a fourth independent algorithm for the one quantity where implementations have real room to disagree |
| P20 | `std::optional` turns finding three from a convention into a type |
| P04 | `std::variant` reaches the same guarantee the Rust gets from an enum, and charges more for it |
| P07 | an allocator, again: a parameter pair and a refusal disappear |
| P10 | `std::variant`, again, on a promise the C's own header makes in prose |
| P11 | **a sentinel return and a sentinel terminated array, both replaced by types** |

**The sentinel return.** `at_backoff_ms` answers `AT_BACKOFF_REFUSED`, which is
`0xFFFFFFFFu`, and that is **a legal value of its return type**: a caller that
forgets to compare gets a delay of about forty-nine days rather than an error.
Here the answer is `std::optional<std::uint32_t>` and the value cannot be read
without the question being answered. That is **P20's answer a second time**, and
the shape is the same one: a wrong value nothing downstream can detect, replaced
by a type that will not produce it. The Rust reaches the same place with
`Option<u32>`.

**The sentinel terminated array.** The C takes `const char *const *urcs` and
walks it until it finds a null, so the table's length lives in the table's own
last element and a caller who forgets the terminator reads past the end. Here
the table is a `std::vector<std::string_view>` with a size, and the C's "pass
`NULL` for no table yet" becomes an empty one. Both are the same behaviour and
only one of them can be got wrong by a caller.

**That is all of it, and saying so is part of the answer.** No template helps
here, no allocator is needed, and `std::string_view` removes the `strlen` and
`strncmp` pair without changing a comparison. This project sits closer to P17
than to P12: the language tidies the interface and leaves the arithmetic exactly
where it was.

## What is identical

The two tables and their contents; the prefix match rather than an equality
test, which is what lets `+CME ERROR: 10` and `CONNECT 115200` classify
correctly and what makes `OKAY` a success; the order the three classes are tried
in; the empty line as its own answer; the unsigned subtraction read as signed,
and its real bound of about twenty-four days; the doubling that stops at the
cap; the scatter applied **after** the cap, so the answer can exceed it by up to
an eighth; and the span falling back to one where an interval is too small to
take a quarter from.

One line is written differently for a reason that is about warnings rather than
about the chapter. The C's doubling loop counts in `uint8_t`; here the counter
is `unsigned`, because incrementing a `uint8_t` promotes to `int` and narrows
back, and this repository compiles with `-Wconversion` and `-Wsign-conversion`.
The bound is the same: `attempt` is a byte, so the loop runs at most 255 times
in both.

C++20 would spell the prefix test `line.starts_with(prefix)`. This is C++17, so
it is `line.compare(0, prefix.size(), prefix) == 0` with the length checked
first, which is the same comparison the C's `strncmp` makes.

## The filter protocol

```text
C <n_urcs> <urc field>*n <line field>   -> class=final_ok | final_bad | urc
                                                 | intermediate | empty
D <now_ms> <deadline_ms>                -> due=1 | due=0
B <attempt> <base_ms> <cap_ms> <rand>   -> delay=NNN | REFUSED backoff
```

**A field is a length and then hex, and the oracle is the reason rather than a
taste for hex.** Nine of the seventeen classify cases contain a space, four
contain a comma or a double quote, and one is the **empty** line, which is a
class of its own here, so **ten of the seventeen cannot cross a
whitespace-separated protocol at all** and only seven can. A filter that cannot
carry a case cannot test it. The length travels with the hex because a
whitespace split cannot produce an empty token, and a hex token whose decoded
length disagrees with the stated one is refused rather than classified as the
shorter line.

**The refusal crosses as a word and not as `0xFFFFFFFF`,** for the same reason
the function returns an optional: carried as a number it would be
indistinguishable from an answer.

The prefixes own their bytes and the views point into them, so the vector of
strings outlives the vector of views and both live in the same function. That is
the one cost of `string_view` in this file and it is worth naming: a view is
cheap to pass and easy to leave dangling.

## What is not here

The engine itself, its queue, the completion callback that must fire exactly
once in every outcome, the unsolicited handler table and the hundred injected
faults. Those need the module, the byte path of chapter 4 and the framing of
chapter 5. There is no measured number in this file.
