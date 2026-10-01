# A 40-bit payload, encoded in C and decoded in Python

    bit 39                                                            bit 0
    +-----+------+-----------+--------------------------------+------------+
    | ver | flags|  sequence |            feature             |  battery   |
    |  3  |   4  |     9     |          18, signed            |     6      |
    +-----+------+-----------+--------------------------------+------------+
    |    byte 0   |   byte 1  |   byte 2   |   byte 3   |     byte 4       |

Forty bits, five bytes, no padding, most significant bit first.
`BITORDER.md` is the normative statement of that and was written before any
implementation existed.

Chapter 9 of the NUCLEO-H7A3ZI-Q firmware volume. One specification written as
data, a C encoder and a Python decoder generated from it, and golden vectors
computed by hand that no generator has ever touched.

**Started and proven Thursday 1 October 2026.** The whole host-side half is
built, tested and green. Nothing has run on the board, because win11 aquamarine
has no cross compiler and no flashing tool, and that is the only thing missing.

This is the chapter to do first, and the reason is structural rather than a
preference: a codec has no timing, no peripherals and no state, so it is the one
piece of firmware that can be tested completely with no hardware at all. Unlike
chapter 6, it has no value that needs confirming against RM0455 and therefore no
back end that refuses to run.

## What this claims, and what would refute it

The claim is that the layout in `BITORDER.md` is implemented identically by
three independent code paths and that all three agree with a specification
written before any of them existed.

Four things would refute it, and each has a test that can fail:

| The claim | What refutes it | Where |
|---|---|---|
| The implementations match the specification | Any implementation disagreeing with a hand-computed vector | `tests/test_vectors.py` |
| The implementations match each other | Any byte differing across C, Python and C++ over 100000 cases | `tests/test_roundtrip.py`, `tests/test_cpp.py` |
| A specification change cannot be merged half-applied | The generated files differing from the committed ones | `tools/gen_codec.py --check` |
| One decoder source serves host and board | `../../firmkit/payload.py` drifting out of the MicroPython subset | `tests/test_micropython_subset.py` |

**Why the vectors matter more than the round trip.** The field table generates
both the C header and the Python module, which removes the chance of the two
drifting apart and also removes their independence. Two outputs of one generator
agreeing proves that the generator is self-consistent and nothing more. The
vectors are computed on paper from the written bit order, and they are what prove
the layout is the one that was intended.

## What runs today, with no board and no cross compiler

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python tools/build_host.py
```

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python -m pytest tests -q
```

Twenty-one checks, about six seconds, 100000 random cases per property. The
first command uses the compiler directly because win11 aquamarine has CMake but
no `make` and no `ninja`; `CMakeLists.txt` carries the chapter's published build
line for a machine that has a generator.

## A defect in the published chapter

Chapter 9 prints its second golden vector as

    {"fields": {"version": 7, "flags": 15, "sequence": 511,
                "feature": 131071, "battery": 63},
     "bytes": "FFFFFFFFFF"}

Those two halves contradict each other. `feature` is an 18-bit two's complement
field, so its largest positive value, 131071, is a zero followed by seventeen
ones. That zero is the top bit of byte 2, so the correct bytes are `FFFF7FFFFF`.
`FFFFFFFFFF` decodes to `feature = -1`.

Both cases are real and both are in `vectors.json` as separate entries, with
`test_the_two_maximum_cases_are_different` to stop anyone merging them back
together. The chapter's own rule applies to the chapter: the hand computation
wins and the vector is not changed to match an implementation.

The irony is worth recording rather than smoothing over. The chapter calls the
all-ones case "the one that catches a width error", and in printing it made a
sign error instead.

## Measured, not asserted

### What CBOR would have cost

The chapter's budget table carries "the same content as CBOR, about 17 B by
arithmetic, not measured". Measured over the six vectors and 100000 random cases
with seed 20260920, payload only, before framing:

| Shape | Min | Max | Mean | Against packed |
|---|---|---|---|---|
| Bit-packed, this chapter | 5 | 5 | 5.00 | 1.00x |
| CBOR map, full field names | 45 | 52 | 50.07 | 10.01x |
| CBOR map, one-letter keys | 16 | 23 | 21.07 | 4.21x |
| CBOR array, positional | 6 | 13 | 11.07 | 2.21x |

Reproduce with `python firmkit/cbor.py`. The encoding is done against
RFC 8949's canonical shortest-form rule rather than through a library, because
none of the three named libraries is installed and a library would add its own
framing choices without making the comparison fairer.

Two corrections to the chapter follow. The estimate of about 17 bytes is low: the
nearest shape, a map with one-letter keys, measures 21.07 bytes on average. And
the chapter's "about three times that" is only true if the shape is left
unstated, since the honest range across shapes is 2.21x to 10.01x.

**The decision stands, and the reason is now a number.** Framed, bit-packing is
9 bytes against 25.07 for the self-describing shape anyone would actually ship,
which is nearly three times the air time on a link where air time is the budget.
What would reverse it: a link where air time is not the constraint, a payload
that has to be read by a party without the header, or a schema changing more than
once a year.

### The two implementations, by size

Host x86-64 objects at `-Os`, one translation unit each holding one encoder and
one decoder and nothing else:

| Implementation | Object size |
|---|---|
| C, `../../shared/payload.c` | 1518 bytes |
| C++17, `../../shared/payload.hpp` via `cpp_encode_only.cpp` | 2700 bytes |

**Published, not ranked.** These are host objects and they are not the flash cost
on the board, which needs `arm-none-eabi-size` and therefore a toolchain this
laptop does not have. The budget table's flash row stays "not measured" until it
can be measured. What the C++ variant buys for its larger host object is the
layout checked at compile time: `encode` and `decode` are `constexpr`, so the
fourth golden vector is asserted by the compiler in `../../shared/payload.hpp` and a
width changed in one place and not the other fails to build.

### What the test suite can and cannot see

Nine deliberate defects were introduced one at a time and the suite re-run. Seven
were caught. The two survivors are recorded because they say something true about
the code:

| Mutation | Caught | Why |
|---|---|---|
| Bit order reversed inside a field | yes, 5 tests | |
| Sign extension removed | yes, 2 tests | only the negative vector and the round trip see it |
| Two fields swapped in the encoder | yes, 5 tests | |
| Decoder reads a field one bit off | yes, 2 tests | |
| Encoder truncates `flags` to 3 bits | yes, 5 tests | |
| C++ layout drifts from the specification | yes, at compile time | two static assertions |
| Specification changed, files not regenerated | yes, the `--check` gate | |
| A field mask widened by one bit | **no** | `bw_put` already truncates to `width`, so the masks in `payload_encode` cannot change the output. Equivalent mutant, not a gap |
| The `memset` removed from the encoder | **no** | `bw_put` clears as well as sets, so all forty bits are written explicitly. The `memset` guards against widths that do not sum to the byte count, which the static assertions already prevent |

Both survivors are second layers of defence against something an outer layer
already prevents, so no test can observe them. Both stay. What the second case
did produce is a test that was missing: `test_encoder_does_not_write_past_its_five_bytes`,
which pre-dirties the buffer and checks the tail comes back untouched.

## Layout

    fields.py              the specification, and the only place it lives
    ../../shared/payload_fields.h       generated for C, committed
    ../../firmkit/payload_fields.py      generated for Python, committed
    ../../shared/payload.c              the bit writer and reader in C
    ../../shared/payload.h              the whole interface: two functions and a buffer
    ../../shared/payload.hpp            the C++ variant, widths as template parameters
    ../../firmkit/payload.py            the Python twin: oracle, edge host, and board
    tools/gen_codec.py           the generator, and its --check gate
    tools/build_host.py          the build that works without make or ninja
    firmkit/cbor.py           the comparison the chapter left unmeasured
    BITORDER.md             normative, written before any implementation
    vectors.json            hand computed, never generated
    tests/test_vectors.py         both sides against the vectors
    tests/test_roundtrip.py       the property, 100000 cases, recorded seed
    tests/test_cpp.py             the C++ variant against C over the whole run
    tests/test_micropython_subset.py  keeps the one-source claim honest
    cpp_filter.cpp          the C++ variant as a filter the tests drive
    cpp_encode_only.cpp     one translation unit, for the size comparison
    main.c                   the board side, written, never compiled
    listen.py               the edge-host variant, needs chapter 5
    MICROPYTHON.md        why there is no file in that directory

## Two places this deviates from the chapter, deliberately

**The C++ variant is driven as a filter from Python**, rather than being a C++
test that parses `vectors.json`. Parsing JSON in C++ with no dependency is a
lot of code that tests nothing, and it would have limited the C++ comparison to
six cases. Driving it from Python compares all three implementations over the
whole 100000-case run, which is what the acceptance criteria actually ask for.

**`tools/build_host.py` exists alongside `CMakeLists.txt`.** The chapter's build
line is right on a machine with a generator. This laptop has none, and a chapter
that could be finished today should not wait on a download. The script builds the
same two translation units and will be deleted when `ninja` is installed.

## Two traps this repository hit, recorded

**A stale `__pycache__` shadowed the specification.** `tools/gen_codec.py`
originally loaded `fields.py` through `importlib`, which goes through the
bytecode cache. The cache is keyed on the source file's size and modification
time, and an edit changing a width from 18 to 17 leaves the size identical, so a
stale entry can shadow the real file and the generator emits headers for a layout
nobody wrote. It took a contradiction between `grep` and the generator to notice.
The generator now compiles the text on disk. For a file that is the
specification, deterministic beats idiomatic.

**Converting an out-of-range unsigned to a signed type is implementation
defined**, which is the same class of defect as the arithmetic shift that
`sign_extend` exists to avoid. Writing `(int32_t)(v - (1u << width))` looks
tidier than the subtraction in signed arithmetic and trades one undefined
behaviour for another. Both the C and the C++ versions do it the long way, with a
comment saying why.

## What is not done

- Nothing has been compiled for the target or flashed. `main.c` is written
  and honest about it. It needs `arm-none-eabi-gcc`, a flashing tool, and
  chapter 1's startup code, linker script and `board_init`.
- `listen.py` needs chapter 5's byte stuffing and its CRC-16, whose
  polynomial must match the peripheral's configuration. Both are stubbed with a
  `NotImplementedError` and a note rather than written from memory, because a
  frame decoder written twice from recollection is how two implementations of one
  format come to disagree.
- The MicroPython variant has not run on the board. The subset constraints are
  asserted by test; the execution is not.
- The flash and cycle rows of the budget table are still "not measured", and they
  stay that way until there is a toolchain and chapter 6's counter.

## Sources

Normative:

- `BITORDER.md` in this repository, which is normative for both
  implementations and for the vectors.
- The C standard, for the rules on shifting signed integers and on conversion to
  unsigned that the encoder depends on.
- RFC 8949, for the CBOR head encoding and the canonical shortest-form rule used
  by `firmkit/cbor.py`.

Read, and deliberately not used:

- NanoCBOR, CC0, the smallest of the three encoders.
  <https://github.com/bergzand/NanoCBOR>
- zcbor, Apache-2.0, schema-driven and used in production.
  <https://github.com/NordicSemiconductor/zcbor>
- TinyCBOR, MIT, the minimalist alternative.
  <https://github.com/intel/tinycbor>
- CrustyAuklet's bit packer, MIT, which shares one format string between a C++
  implementation and a Python counterpart. It is the closest thing to this
  repository that exists, and it is C++14 where this chapter is C, which is why
  it is read rather than depended on and why the C++ variant exists.
  <https://github.com/CrustyAuklet/bitpacker>
