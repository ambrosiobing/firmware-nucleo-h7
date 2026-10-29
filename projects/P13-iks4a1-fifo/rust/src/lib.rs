//! P13's buffer decode, its drop count and its fill-to-drain ratio, `no_std`.
//!
//! The same three pieces, with the same arithmetic in the same order, as
//! `../../c/fifodecode.c` and `../../python/fifodecode.py`, against the same
//! oracle. The four findings this project is about are written out in
//! [`../../c/fifodecode.h`](../../c/fifodecode.h) and are not repeated here:
//! the chapter's drop count is computed in the wrong modulus, it fabricates
//! drops at startup, a gap of exactly the counter's modulus is invisible, and
//! the fill time divides by one source's rate while the buffer holds several.
//!
//! WHAT THIS CRATE ADDS IS FINDING TWO IN THE TYPE SYSTEM, and it is the first
//! time in this volume that a type has carried a FINDING rather than a refusal.
//! The C's state holds two fields for one fact:
//!
//! ```c
//! uint8_t expect_cnt;  /* carried between drains */
//! uint8_t started;     /* 0 until the first word is seen */
//! ```
//!
//! Finding two is that the chapter has no `started` at all: its `expect_cnt` is
//! a static beginning at zero, so the first word of the first drain is measured
//! against a zero nobody wrote and the logger opens by reporting drops that
//! never happened. The C's answer adds the flag. **But the pair can disagree**:
//! `started` set with no expectation, or an expectation while `started` is
//! clear, are both representable, and nothing in the type stops a later edit
//! producing either. Here it is one field:
//!
//! ```text
//! expect: Option<u8>
//! ```
//!
//! `None` IS "no word has been seen yet", so the two states cannot disagree,
//! and the comparison against an expectation cannot be written without first
//! taking the expectation out of the option. That is the same mechanism as
//! P20's unresolved percentile, P11's refused backoff and P10's refusal
//! carrying no ledger, applied to state carried between calls.
//!
//! THE SLICE CARRIES ITS OWN LENGTH, which is the other difference and it is
//! about memory rather than arithmetic. The C takes `const uint8_t *buf` with a
//! separate `words`, and its header says "words is the buffer level already
//! clamped by the caller", so a `words` larger than the buffer reads past the
//! end: undefined behaviour rather than a wrong answer. Here a buffer too short
//! for the stated word count is `Error::Argument`. **No oracle case reaches
//! that**, because the oracle's buffers are built to match, so this is not a
//! new case the C would have to answer; it is the same refusal name used for a
//! condition the C cannot detect.
//!
//! The budget is `f64` and this crate is still `no_std`: addition,
//! multiplication, division and comparison are all in core. P06's and P18's
//! crates are std because `sqrt` and `log10` are not, and P12's and P10's write
//! a magnitude out because `abs` is not. None of the three appears here.
#![cfg_attr(not(test), no_std)]

/// A five-bit tag field is the widest this decode tabulates, so the counts fit
/// a fixed array and an unknown tag is a count rather than an index into
/// nothing.
pub const TAG_VALUES: usize = 32;
pub const MAX_WORD_BYTES: u8 = 16;
pub const MAX_WORDS: u16 = 1024;
pub const MAX_SOURCES: usize = 8;

/// The same five refusals as the C, in the same order and with its names.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// A null pointer in the C; here, a buffer shorter than the stated words.
    Argument,
    /// A word, shift or counter width out of range.
    Format,
    /// More words than the landing buffer holds.
    Words,
    /// A batching rate or bus rate at or below zero.
    Rate,
    /// No sources, or more than the bound allows.
    Sources,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Format => "format",
            Error::Words => "words",
            Error::Rate => "rate",
            Error::Sources => "sources",
        }
    }
}

/// The word layout, a parameter and not a constant: the chapter states only the
/// word shape, a tag byte and six data bytes, and sends the tag format to each
/// sensor's own datasheet. Every finding holds for any counter field narrower
/// than a byte.
#[derive(Debug, Clone, Copy)]
pub struct Format {
    pub word_bytes: u8,
    pub tag_shift: u8,
    pub cnt_shift: u8,
    pub cnt_bits: u8,
    pub known_tags: u32,
}

impl Format {
    fn is_valid(&self) -> bool {
        if self.word_bytes == 0 || self.word_bytes > MAX_WORD_BYTES {
            return false;
        }
        if self.cnt_bits == 0 || self.cnt_bits > 8 {
            return false;
        }
        if self.tag_shift > 7 || self.cnt_shift > 7 {
            return false;
        }
        // The tag field is whatever remains above tag_shift, so a shift that
        // leaves room for more values than the table has is a format this
        // cannot tabulate rather than one to silently truncate.
        (0xFFu32 >> self.tag_shift) < TAG_VALUES as u32
    }
}

/// What a drain leaves behind, carried into the next one.
#[derive(Debug, Clone)]
pub struct State {
    pub words: u16,
    pub per_tag: [u16; TAG_VALUES],
    pub unknown_tag: u16,
    pub drops: u32,
    pub gaps: u16,
    /// The counter value the next word should carry, or `None` before any word
    /// has been seen. This single field is finding two: see the crate comment.
    pub expect: Option<u8>,
}

impl Default for State {
    fn default() -> Self {
        State {
            words: 0,
            per_tag: [0; TAG_VALUES],
            unknown_tag: 0,
            drops: 0,
            gaps: 0,
            expect: None,
        }
    }
}

impl State {
    /// The reset state, which is what the chapter's static variable cannot be.
    pub fn new() -> Self {
        State::default()
    }
}

fn mask_of(cnt_bits: u8) -> u32 {
    if cnt_bits >= 8 {
        0xFF
    } else {
        (1u32 << cnt_bits) - 1
    }
}

/// The gap between an expected and an observed counter value, **in the
/// counter's own modulus**. This is the corrected form of step 7's
/// subtraction, which wraps at 256 whatever the field's width is: a two-bit
/// counter expecting 3 and seeing 0 has advanced by one, and the chapter's line
/// adds 253.
pub fn cnt_gap(expect: u8, cnt: u8, cnt_bits: u8) -> u8 {
    if cnt_bits == 0 || cnt_bits > 8 {
        return 0;
    }
    // Wrapping, because the subtraction is the point: cnt below expect is the
    // wrap this function exists to measure, and the dev profile's overflow
    // checks would panic on it. That is the fifth instance in this workspace.
    let gap = u32::from(cnt).wrapping_sub(u32::from(expect)) & mask_of(cnt_bits);
    gap as u8
}

/// Decode one drained block into `state`.
///
/// `words` is the buffer level, and a value above [`MAX_WORDS`] is refused
/// rather than clamped, because silently shortening a read is how a level
/// register drives a decode past the end of its landing buffer.
pub fn decode(fmt: &Format, buf: &[u8], words: u16, state: &mut State) -> Result<(), Error> {
    if !fmt.is_valid() {
        return Err(Error::Format);
    }
    if words > MAX_WORDS {
        return Err(Error::Words);
    }
    let needed = usize::from(words) * usize::from(fmt.word_bytes);
    if buf.len() < needed {
        // The condition the C cannot detect; see the crate comment.
        return Err(Error::Argument);
    }

    let cnt_mask = mask_of(fmt.cnt_bits);
    for i in 0..usize::from(words) {
        let first = u32::from(buf[i * usize::from(fmt.word_bytes)]);
        let tag = (first >> fmt.tag_shift) as usize;
        let cnt = ((first >> fmt.cnt_shift) & cnt_mask) as u8;

        if (fmt.known_tags >> tag) & 1 != 0 {
            state.per_tag[tag] += 1;
        } else {
            state.unknown_tag += 1;
        }

        // The first word of the first drain establishes the sequence instead of
        // being measured against a zero nobody wrote. In the C this is a flag
        // beside the expectation; here it is the absence of the expectation.
        if let Some(expect) = state.expect {
            let gap = cnt_gap(expect, cnt, fmt.cnt_bits);
            if gap != 0 {
                state.drops += u32::from(gap);
                state.gaps += 1;
            }
        }
        state.expect = Some(((u32::from(cnt) + 1) & cnt_mask) as u8);
        state.words += 1;
    }
    Ok(())
}

/// Everything step 8's ratio needs, with every assumption a field.
#[derive(Debug, Clone, Copy)]
pub struct Spec {
    pub watermark_words: u16,
    pub sources: u8,
    pub word_bytes: u8,
    pub cnt_bits: u8,
    pub batch_hz: [f64; MAX_SOURCES],
    pub bus_hz: f64,
    pub decode_us_per_word: f64,
}

/// The fill-to-drain ratio, with the chapter's own form reported beside it.
#[derive(Debug, Clone, Copy)]
pub struct Budget {
    pub entries_per_s: f64,
    pub fill_ms: f64,
    pub chapter_fill_ms: f64,
    pub drain_us: f64,
    pub ratio: f64,
    /// `1 << cnt_bits`, finding three: that many consecutive lost slots read as
    /// no loss at all, which is a bound on what the drop count can see and
    /// belongs beside the number.
    pub undetectable_gap: u32,
    pub ratio_ok: bool,
}

/// The C's `!(x > 0.0)`, written out so the comparison is the same one: it
/// refuses zero, every negative value and any value that is not a number.
/// clippy rejects the C's spelling as `neg_cmp_op_on_partial_ord`.
fn not_above_zero(x: f64) -> bool {
    x.is_nan() || x <= 0.0
}

fn below_zero(x: f64) -> bool {
    x.is_nan() || x < 0.0
}

pub fn budget(spec: &Spec) -> Result<Budget, Error> {
    if spec.sources == 0 || usize::from(spec.sources) > MAX_SOURCES {
        return Err(Error::Sources);
    }
    if spec.word_bytes == 0 || spec.word_bytes > MAX_WORD_BYTES {
        return Err(Error::Format);
    }
    if spec.cnt_bits == 0 || spec.cnt_bits > 8 {
        return Err(Error::Format);
    }
    if spec.watermark_words == 0 || spec.watermark_words > MAX_WORDS {
        return Err(Error::Words);
    }
    if not_above_zero(spec.bus_hz) || below_zero(spec.decode_us_per_word) {
        return Err(Error::Rate);
    }
    let rates = &spec.batch_hz[..usize::from(spec.sources)];
    for r in rates {
        if not_above_zero(*r) {
            return Err(Error::Rate);
        }
    }

    // Every batched source writes its own entries into one buffer, so the
    // buffer fills at the SUM of the rates. The chapter's printed form divides
    // by one of them, and both are reported. Accumulated in the array's order,
    // which is part of the specification: the oracle compares exactly.
    let mut entries = 0.0;
    for r in rates {
        entries += *r;
    }
    let fill_ms = 1000.0 * f64::from(spec.watermark_words) / entries;
    let chapter_fill_ms = 1000.0 * f64::from(spec.watermark_words) / rates[0];

    // The transaction, as a stated model rather than a measurement: every byte
    // on this bus costs nine bits with its acknowledgement, the register
    // address and the device address are two more bytes, and a start and a stop
    // bracket the whole thing. The decode cost is measured on the board with
    // the cycle counter and arrives here as a parameter.
    let bytes = f64::from(spec.watermark_words) * f64::from(spec.word_bytes);
    let bits = (bytes + 2.0) * 9.0 + 2.0;
    let drain_us =
        1.0e6 * bits / spec.bus_hz + f64::from(spec.watermark_words) * spec.decode_us_per_word;

    let ratio = (fill_ms * 1000.0) / drain_us;
    let undetectable_gap = if spec.cnt_bits >= 8 {
        256
    } else {
        1u32 << spec.cnt_bits
    };
    Ok(Budget {
        entries_per_s: entries,
        fill_ms,
        chapter_fill_ms,
        drain_us,
        ratio,
        undetectable_gap,
        ratio_ok: ratio >= 10.0,
    })
}

#[cfg(test)]
mod tests {
    use super::{budget, cnt_gap, decode, Error, Format, Spec, State};

    fn fmt() -> Format {
        Format {
            word_bytes: 7,
            tag_shift: 3,
            cnt_shift: 1,
            cnt_bits: 2,
            known_tags: 6,
        }
    }

    fn block(first: &[u8], word_bytes: u8) -> [u8; 64] {
        let mut out = [0u8; 64];
        for (i, b) in first.iter().enumerate() {
            out[i * usize::from(word_bytes)] = *b;
        }
        out
    }

    #[test]
    fn the_gap_is_taken_in_the_fields_modulus_and_not_the_bytes() {
        // A two-bit counter expecting 3 and seeing 0 has advanced by one. The
        // chapter's subtraction modulo 256 answers 253.
        assert_eq!(cnt_gap(3, 0, 2), 1);
        assert_eq!(0u8.wrapping_sub(3), 253);
        // With an eight-bit field the two forms agree, which is why a wrong
        // modulus can hide behind the widest counter.
        assert_eq!(cnt_gap(3, 0, 8), 253);
    }

    #[test]
    fn the_first_word_establishes_the_sequence_rather_than_being_measured() {
        let f = fmt();
        let buf = block(&[8], f.word_bytes);
        let mut state = State::new();
        assert_eq!(state.expect, None);
        decode(&f, &buf, 1, &mut state).expect("one word of a known tag");
        assert_eq!(state.drops, 0, "the chapter reports the counter value here");
        assert_eq!(state.words, 1);
        assert_eq!(state.expect, Some(1));
    }

    #[test]
    fn a_gap_of_exactly_the_modulus_is_invisible_and_that_is_finding_three() {
        let mut spec = spec_of();
        spec.cnt_bits = 2;
        let b = budget(&spec).expect("the chapter's own numbers");
        assert_eq!(b.undetectable_gap, 4);
        spec.cnt_bits = 8;
        assert_eq!(budget(&spec).unwrap().undetectable_gap, 256);
    }

    fn spec_of() -> Spec {
        let mut batch_hz = [0.0; super::MAX_SOURCES];
        batch_hz[0] = 104.0;
        batch_hz[1] = 104.0;
        Spec {
            watermark_words: 128,
            sources: 2,
            word_bytes: 7,
            cnt_bits: 2,
            batch_hz,
            bus_hz: 400000.0,
            decode_us_per_word: 0.0,
        }
    }

    #[test]
    fn the_fill_time_halves_where_two_sources_batch_into_one_buffer() {
        let b = budget(&spec_of()).expect("the chapter's own numbers");
        assert_eq!(b.entries_per_s, 208.0);
        // Step 8 prints 1230 ms, which is 128 / 104. Two sources fill it at
        // 208 entries a second, so the fill time is half what is printed.
        assert_eq!(b.chapter_fill_ms, b.fill_ms * 2.0);
        assert!(b.ratio_ok, "the order of magnitude is there even halved");
    }

    #[test]
    fn the_refusals_are_the_cs_refusals() {
        let mut f = fmt();
        f.word_bytes = 0;
        let buf = [0u8; 8];
        let mut state = State::new();
        assert_eq!(decode(&f, &buf, 1, &mut state), Err(Error::Format));
        assert_eq!(decode(&fmt(), &buf, 2000, &mut state), Err(Error::Words));
        // The one the C cannot detect: two words of seven bytes need fourteen.
        assert_eq!(decode(&fmt(), &buf, 2, &mut state), Err(Error::Argument));

        let mut spec = spec_of();
        spec.sources = 0;
        assert_eq!(budget(&spec), Err(Error::Sources));
        let mut spec = spec_of();
        spec.bus_hz = 0.0;
        assert_eq!(budget(&spec), Err(Error::Rate));
    }

    #[test]
    fn an_unknown_tag_is_counted_rather_than_indexing_into_nothing() {
        let f = fmt();
        // Tag 0 is not in known_tags, which is 6, so bits 1 and 2.
        let buf = block(&[0, 8], f.word_bytes);
        let mut state = State::new();
        decode(&f, &buf, 2, &mut state).expect("two words");
        assert_eq!(state.unknown_tag, 1);
        assert_eq!(state.per_tag[1], 1);
        assert_eq!(state.words, 2);
    }
}
