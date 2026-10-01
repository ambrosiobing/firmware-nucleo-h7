# The same decoder, on the board, under MicroPython

There is no file in this directory, and that is the point of the variant.

`codec/payload.py` and the generated `codec/payload_fields.py` are copied to the
board unchanged. A copy kept here would be a second file to keep in step, which
is the exact failure mode the whole chapter is arranged against.

`test/test_micropython_subset.py` is what keeps the claim honest. It asserts, on
every test run and with no board attached, that `payload.py` has no f-strings, no
annotations, no `__future__` import, and no import beyond the generated field
table. Those are the constraints the interpreter imposes. The test does not prove
the file runs on the board, which needs the board; it proves the file has not
drifted out of the subset since somebody last checked that it did.

## The path, when the board is in hand

MicroPython has an official board definition for this exact part in its main
branch, with its own linker script, and prebuilt firmware is published, so this
variant costs a drag-and-drop and no build at all.

1. Copy the published firmware to the probe's mass storage disk, which chapter 1
   covers.
2. Copy the two files across with `mpremote`.
3. Ask the board to decode the fourth golden vector, which is the negative one.

```bash
mpremote cp codec/payload.py :payload.py
```

```bash
mpremote cp codec/payload_fields.py :payload_fields.py
```

```bash
mpremote exec "import payload; print(payload.decode(bytes.fromhex('2405FFFFE8')))"
```

The expected answer is the fourth entry of `test/vectors.json`:

    {'version': 1, 'flags': 2, 'sequence': 5, 'feature': -1, 'battery': 40}

The negative vector is the one to run, not an easy one. `feature` coming back as
`-1` rather than `262143` is the whole content of the check, because sign
extension is what a second implementation gets wrong and it works for every
positive test value.

## Two constraints, stated before anyone meets them at the prompt

The decoder must stay inside the interpreter's subset. For this module that means
no f-string formatting in the hot path and no annotations needing a module the
firmware does not carry. Both are asserted by the test suite rather than left to
discipline.

The board definition's declared feature list is empty, so nothing here should
promise networking or USB host on that firmware without building it.

## What this variant actually proves

One decoder source serving the host and the target. Not that MicroPython is a
good choice for this job: it is not, and the cost is determinism and direct
control of transfers. The variant exists to show the trade, and
`tools/cbor_size.py` plus the size table in the top-level README are the other
two places this repository puts a number against a decision instead of an
opinion.
