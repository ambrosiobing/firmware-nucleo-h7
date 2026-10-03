# P05 in C++

**State: host only, proven Saturday 3 October 2026.** Written on win11
aquamarine, which compiles nothing, then built and proven the same day: `g++` in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and then CI at commit 6e8ae9d, where it is
also compiled with the full warning set and `-Werror`. The `static_assert` of
0x29B1 has been seen to pass, which is what that state now rests on.

| File | What it is |
|---|---|
| `frame.hpp` | the whole implementation, header only: `crc16`, COBS, the frame, and the verdict names |
| `frame_filter.cpp` | the filter `build_host.py` builds to `build-host/frame_filter`, speaking three verbs: `C hex`, `E hex`, `D hex` |

## What C++ adds, and it is one thing

`crc16` is `constexpr`, so the published check value 0x29B1 over `"123456789"`
is a `static_assert` at the bottom of the header. The C asserts the same value
in a test; here a translation unit that includes the header does not compile
unless the six parameters are the published ones. That is the property P09's
C++ has for its fourth golden vector, and it is the reason this is a header
rather than a `.cpp`.

The subset is the one every firmware C++ in this volume keeps: no exceptions,
no RTTI, no heap, no iostream, `std::array` for the buffers whose size the
format fixes. Built with `-fno-exceptions -fno-rtti`.

## The one place it departs from the C, deliberately

`cobs_decode` returns `std::optional<std::size_t>` where the C returns 0 for
failure. The C cannot tell a corrupt frame from a frame that decodes to zero
bytes, which the single byte `0x01` does, legitimately, and so the C's
`frame_decode` reports that frame as a stuffing error when it is a frame too
short to carry its checksum. The Python twin tells the two apart with `None`;
this does with `std::nullopt`. Both answers are refusals, so the chapter's
claim stands; the name differs for exactly one input, and
`python/tests/test_frame_parity.py` pins that rather than hiding it, so that
correcting the C turns a test red on purpose.

## What it is proven against

The project's oracle, in three parts, through `python/tests/test_frame_parity.py`
and the same three-verb protocol the Rust filter speaks: 0x29B1 first, then
identical frames to the C over 3000 random payloads and the cases byte stuffing
exists for, then identical verdicts to the C over 3000 frames corrupted three
ways. Nothing here parses the random cases or the corruption set; the test owns
them.

## What it does not do yet

- Not built for the board. `frame.hpp` would compile for the target as it is,
  since it includes nothing the target lacks, and that is a claim until
  `add_firmware()` has a C++ target, which is P01's C++ half.
- No size comparison against the C. That is a measurement and needs the board.
