// projects/P11-at-engine/cpp/atparse.hpp: the line classifier, the wrap-safe
// deadline test and the backoff, in C++17.
//
// The same three pieces, with the same arithmetic in the same order, as
// `../c/atparse.c`, `../python/atparse.py` and `../rust/src/lib.rs`. The
// reasoning is in `../c/atparse.h` and is not repeated here, including the one
// that decides the project: chapter 11's deadline test is a signed difference
// on a 32 bit counter, the obvious form is correct for forty-nine days and then
// is not, and A BOARD CANNOT DEMONSTRATE THAT WITHOUT WAITING FORTY-NINE DAYS.
//
// WHAT C++ ADDS HERE IS TWO SMALL THINGS AND BOTH ARE THE SAME THING. The
// volume's answers so far, kept accurate because the question is only worth
// asking once per project if the answer can come back different:
//
//   P18  one function template where the C needs two forty-line copies
//   P12  the C's lack of an allocator costs it sixty lines of fixed buffers,
//        bounds checks and an insertion sort
//   P17  almost nothing: four small functions over fixed-width integers
//   P16  a fourth independent algorithm for the one quantity where
//        implementations have real room to disagree
//   P20  std::optional turns finding three from a convention into a type
//   P04  std::variant reaches the same guarantee the Rust gets from an enum
//   P07  an allocator, again: a parameter pair and a refusal disappear
//   P10  std::variant, again, on a promise the C's header makes in prose
//   P11  two C CONVENTIONS BECOME TYPES: a sentinel return and a sentinel
//        terminated array
//
// THE SENTINEL RETURN. `at_backoff_ms` answers `AT_BACKOFF_REFUSED`, which is
// `0xFFFFFFFFu`, and that is **a legal value of its return type**: a caller
// that forgets to compare gets a delay of about forty-nine days rather than an
// error. Here the answer is `std::optional<std::uint32_t>` and the value cannot
// be read without the question being answered. That is P20's answer a second
// time, and the shape is the same one: a wrong value nothing downstream can
// detect, replaced by a type that will not produce it. The Rust reaches the
// same place with `Option<u32>`, which its README is about.
//
// THE SENTINEL TERMINATED ARRAY. The C takes `const char *const *urcs` and
// walks it until it finds a null, so the table's length lives in the table's
// own last element and a caller that forgets the terminator reads past the end.
// Here the table is a `std::vector<std::string_view>` with a size, and the C's
// "pass NULL for no table yet" becomes an empty one. Both are the same
// behaviour and only one of them can be got wrong by a caller.
//
// THAT IS ALL OF IT, AND SAYING SO IS PART OF THE ANSWER. No template helps
// here, no allocator is needed, and `std::string_view` removes the `strlen` and
// `strncmp` pair without changing a comparison. This project is closer to P17
// than to P12: the language tidies the interface and leaves the arithmetic
// exactly where it was.
//
// EVERY QUANTITY HERE IS AN INTEGER, so no floating point appears and no
// contraction flag applies, which is why the build passes none.
#ifndef P11_ATPARSE_HPP
#define P11_ATPARSE_HPP

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace p11 {

// The three classes of step 5, plus the empty line, which is not a class: a
// module pads its replies with blank lines, and a classifier that called one an
// intermediate result would attach it to whatever command was in flight.
enum class Class {
    kFinalOk = 0,
    kFinalBad = 1,
    kUrc = 2,
    kIntermediate = 3,
    kEmpty = 4,
};

// The oracle's names, which are what the filter prints.
inline const char *name(Class c)
{
    switch (c) {
    case Class::kFinalOk:      return "final_ok";
    case Class::kFinalBad:     return "final_bad";
    case Class::kUrc:          return "urc";
    case Class::kIntermediate: return "intermediate";
    case Class::kEmpty:        return "empty";
    }
    return "unknown";
}

// The tables of chapter 11's step 5, visible here for the same reason the C
// keeps them visible: the module's own manual adds rows, and a table is where
// an addition goes rather than a chain of comparisons.
inline const std::vector<std::string_view> &final_ok_table()
{
    static const std::vector<std::string_view> table = {"OK", "CONNECT"};
    return table;
}

inline const std::vector<std::string_view> &final_bad_table()
{
    static const std::vector<std::string_view> table = {
        "ERROR", "+CME ERROR:", "+CMS ERROR:", "NO CARRIER", "ABORTED"};
    return table;
}

// A prefix match and not an equality test, which matters for exactly one row in
// each table: "+CME ERROR:" is always followed by a number, and "CONNECT" is
// followed by a baud rate on some modules and by nothing on others. Matching
// the whole line would classify "+CME ERROR: 10" as intermediate and attach it
// to the command as data. C++20 would spell this line.starts_with(prefix); this
// is C++17, so the comparison is written out.
inline bool starts_with(std::string_view line, std::string_view prefix)
{
    return line.size() >= prefix.size()
           && line.compare(0, prefix.size(), prefix) == 0;
}

inline bool match_any(std::string_view line,
                      const std::vector<std::string_view> &table)
{
    for (const std::string_view &prefix : table) {
        if (starts_with(line, prefix)) {
            return true;
        }
    }
    return false;
}

// Classify one line, without its line ending. An empty table is the C's null
// table: every line that would have been unsolicited is intermediate, which is
// what an engine whose table is empty does.
inline Class classify(std::string_view line,
                      const std::vector<std::string_view> &urcs)
{
    if (line.empty()) {
        return Class::kEmpty;
    }
    if (match_any(line, final_ok_table())) {
        return Class::kFinalOk;
    }
    if (match_any(line, final_bad_table())) {
        return Class::kFinalBad;
    }
    if (match_any(line, urcs)) {
        return Class::kUrc;
    }
    return Class::kIntermediate;
}

// Has the deadline passed, with now and the deadline both free to have wrapped.
//
// The contract is the C's and is the real one rather than the comfortable one:
// correct as long as the two values are less than about twenty-four days apart,
// which is half the wrap period. "Correct across the wrap" is true only inside
// that bound.
inline bool due(std::uint32_t now_ms, std::uint32_t deadline_ms)
{
    // The subtraction is unsigned, so it wraps rather than being undefined, and
    // only the result is read as signed. On signed values the subtraction
    // itself would be undefined on overflow, which is the version that
    // compiles, passes and then surprises somebody.
    const std::uint32_t diff = now_ms - deadline_ms;
    return static_cast<std::int32_t>(diff) >= 0;
}

// The backoff of step 8: double, cap, then scatter.
//
// THE RANDOM DRAW IS A PARAMETER AND THE CHAPTER'S SKETCH CALLS A FUNCTION. The
// sketch reads at_rand_small() inside, which makes the result unprovable: a
// test can only assert a band, never a value. Taking the draw as an argument
// makes this pure and lets the oracle state exact values.
//
// TWO THINGS THE SKETCH'S ARITHMETIC DOES THAT ARE WORTH KNOWING. The jitter is
// applied after the cap, so **the answer can exceed cap_ms** by up to an eighth
// of it: the cap bounds the doubling and not the result. And a cap above half
// the range is refused, because the doubling would overflow and come back as a
// plausible small number.
inline std::optional<std::uint32_t> backoff_ms(std::uint8_t attempt,
                                               std::uint32_t base_ms,
                                               std::uint32_t cap_ms,
                                               std::uint32_t rand_draw)
{
    if (base_ms == 0u || cap_ms < base_ms) {
        return std::nullopt;
    }
    if (cap_ms > (0xFFFFFFFFu / 2u)) {
        return std::nullopt;
    }

    std::uint32_t d = base_ms;
    // The counter is unsigned int and not uint8_t: incrementing a uint8_t
    // promotes to int and narrows back, which is a conversion this repository
    // compiles with -Wconversion and would rather not argue about.
    for (unsigned i = 0u; i < attempt && d < cap_ms; i++) {
        d <<= 1;
    }
    if (d > cap_ms) {
        d = cap_ms;
    }

    // The scatter, kept exactly as the chapter writes it: take an eighth off,
    // then add a draw from a quarter. The window is a quarter wide and sits an
    // eighth below, so the answer runs from seven eighths of the interval to
    // one and an eighth of it, which is why it can pass the cap.
    const std::uint32_t span = (d >> 2) != 0u ? (d >> 2) : 1u;
    return d - (d >> 3) + (rand_draw % span);
}

}  // namespace p11

#endif  // P11_ATPARSE_HPP
