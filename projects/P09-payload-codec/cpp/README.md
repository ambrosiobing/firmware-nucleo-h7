# P09 in C++

**State: host only, and the first of the four languages to be proven equivalent.**

`payload.hpp` is the codec with the field widths as template parameters, so the
fourth golden vector is asserted by the compiler rather than at run time.
`cpp_encode_only.cpp` exposes it through `extern "C"` for the `ctypes` harness and
`cpp_filter.cpp` is the same code as a filter the tests drive over whole runs.

Proven against `vectors.json` and against the C implementation over the whole
random run, in `python/tests/test_cpp.py`. Not yet built for the target.


## What it is proven against

Vectors.json, four hand-computed golden vectors, the fourth of them negative.
