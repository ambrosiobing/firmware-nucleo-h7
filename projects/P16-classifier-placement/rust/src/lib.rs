//! P16's window features and bounded tree in Rust, against the same oracle as
//! the C and the Python.
//!
//! The same arithmetic, in the same order, as `../../c/winfeat.c`. One window is
//! 128 samples of three axes, which is chapter 16 step 3's definition, and the
//! features are Q15 because that is what the most constrained of the chapter's
//! three implementations can produce.
//!
//! # Why this is a third algorithm and not a third transcription
//!
//! Criterion 1 is an equivalence gate: identical features and identical classes,
//! with the build failing if they differ. The chapter's three implementations are
//! the three placements, in the sensor, on the microcontroller and on the host,
//! and only the host one exists; what the gate compares today is that one written
//! in three languages. A gate like that is worth running in proportion to how
//! independently the implementations were written, and the floored integer
//! square root is the one place here with real room for disagreement. So the
//! three take three routes:
//!
//! * the C: a bit-by-bit descent, written out, because a floating-point `sqrt`
//!   and a cast would agree almost always and almost always is what an exact
//!   gate exists to refuse
//! * the Python: `math.isqrt`
//! * here: `u64::isqrt`, the standard library's, which is exact by contract
//!
//! **That costs this crate a declared minimum Rust version of 1.84 where the
//! other ten in this workspace say 1.75**, because `isqrt` stabilised there. The
//! alternative was to hand-roll one, which would have made this a transcription
//! of the C and would have put an unverified algorithm into the one function the
//! gate most depends on. The pinned toolchain is 1.99.0, so nothing is blocked
//! by the choice; what it costs is one crate out of step with the others, and
//! that is written here rather than left for a reader to notice.
//!
//! # The rounding is pinned, which is this project's finding
//!
//! Step 3 fixes the window, the axes, the feature list and the numeric format
//! and says nothing about rounding. In Q15 that is not enough for a gate that
//! demands equality: a mean truncated toward negative infinity and a mean
//! rounded to the nearest value differ by one least significant bit on most
//! windows. Both obey the printed definition. So the mean and the root mean
//! square here truncate, as they do in the other two, and the alternative is
//! carried in the Python twin for a test to run.
//!
//! # What writing the third language found, which is why it was worth it
//!
//! The root mean square did not saturate, and peak to peak did. Both are Q15,
//! both can exceed what Q15 holds, and the C header's own sentence for peak to
//! peak is that a wrapped value "would read as a quiet window, which is the most
//! misleading answer available here". The same sentence applies to the root mean
//! square and it had not been applied: on a window with every sample at negative
//! full scale the mean of the squares is 1073741824, its root is exactly 32768,
//! and Q15 stops one short of that. The C cast it to `int16_t` and produced
//! negative 32768, the Python returned 32768, which is not a Q15 value at all,
//! and the two had never been compared there because no oracle case reached it.
//! All three now saturate at 32767, and the oracle carries the window that
//! reaches it.
//!
//! That is the gate paying for itself. It compares three implementations for
//! exact equality, and the corner where two of them disagreed was found by
//! writing the third rather than by reading either.
//!
//! This crate is `no_std`: the features are integer arithmetic and the tree walk
//! is a comparison, so nothing here needs a platform.

#![no_std]

pub const WIN_N: usize = 128;
pub const AXES: usize = 3;
pub const GROUPS: usize = 3;
pub const COUNT: usize = GROUPS * AXES;
pub const TREE_MAX: usize = 64;

/// The same six refusals as the C, in the same order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Argument,
    Shape,
    Tree,
    Node,
    Feature,
    Depth,
}

impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Error::Argument => "argument",
            Error::Shape => "shape",
            Error::Tree => "tree",
            Error::Node => "node",
            Error::Feature => "feature",
            Error::Depth => "depth",
        }
    }
}

/// Three groups, one Q15 value per axis each. The band group of step 3 is absent
/// for the reason the C header gives: a quantity named as coming from an
/// unspecified transform cannot enter an exact gate.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Features {
    pub mean: [i16; AXES],
    pub rms: [i16; AXES],
    pub p2p: [i16; AXES],
}

/// A node is a feature index, a threshold and two children. A leaf points at
/// itself and carries its class in the feature field, so a leaf cannot be
/// mistaken for a branch with a missing child.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Node {
    pub feature: u8,
    pub leaf: bool,
    pub threshold: i16,
    pub left: u8,
    pub right: u8,
}

/// Features for one window, interleaved by axis, which is the order the sensor's
/// buffer delivers.
pub fn window(samples: &[i16], n: usize, axes: usize) -> Result<Features, Error> {
    if n == 0 {
        return Err(Error::Argument);
    }
    // Refused rather than scaled: a feature over a different window length is a
    // different feature, and step 3 fixes the length.
    if n != WIN_N || axes != AXES {
        return Err(Error::Shape);
    }
    if samples.len() < n * axes {
        return Err(Error::Shape);
    }

    let mut out = Features {
        mean: [0; AXES],
        rms: [0; AXES],
        p2p: [0; AXES],
    };
    for a in 0..AXES {
        let mut sum: i32 = 0;
        let mut squares: u64 = 0;
        let mut lowest: i32 = 32767;
        let mut highest: i32 = -32768;
        for i in 0..WIN_N {
            let v = i32::from(samples[i * AXES + a]);
            sum += v;
            squares += (i64::from(v) * i64::from(v)) as u64;
            if v < lowest {
                lowest = v;
            }
            if v > highest {
                highest = v;
            }
        }
        // 128 is two to the seventh, so the mean is a shift with a stated
        // direction. Rust's >> on a signed value is already arithmetic, which
        // is the direction the C spells out by hand.
        out.mean[a] = (sum >> 7) as i16;
        // The mean of the squares, floored, then its integer square root, also
        // floored, then saturated. It reaches 32768 on a window of negative
        // full scale, which Q15 stops one short of, and a wrapped root mean
        // square reads as a quiet window for the same reason a wrapped peak to
        // peak does. The C wrapped it and the Python returned a value Q15
        // cannot hold until this crate compared the three.
        let root = (squares >> 7).isqrt();
        out.rms[a] = if root > 32767 { 32767 } else { root as i16 };
        // Peak to peak can reach 65535, which Q15 cannot hold, so it saturates
        // rather than wrapping to a number that would read as a quiet window.
        let span = highest - lowest;
        out.p2p[a] = if span > 32767 { 32767 } else { span as i16 };
    }
    Ok(out)
}

/// The nine features flat, in the order a tree's indices refer to.
pub fn vector(f: &Features) -> [i16; COUNT] {
    let mut out = [0_i16; COUNT];
    out[0..AXES].copy_from_slice(&f.mean);
    out[AXES..2 * AXES].copy_from_slice(&f.rms);
    out[2 * AXES..COUNT].copy_from_slice(&f.p2p);
    out
}

/// Walk the tree and return a class. `depth_max` is the bound step 5 relies on
/// for its latency claim, and exceeding it is a refusal rather than a default.
pub fn classify(nodes: &[Node], depth_max: usize, features: &[i16]) -> Result<u8, Error> {
    if nodes.is_empty() || nodes.len() > TREE_MAX {
        return Err(Error::Tree);
    }
    if depth_max == 0 {
        return Err(Error::Depth);
    }
    if features.is_empty() {
        return Err(Error::Feature);
    }

    let mut at = 0_usize;
    for _ in 0..=depth_max {
        let node = match nodes.get(at) {
            Some(n) => n,
            None => return Err(Error::Node),
        };
        if node.leaf {
            return Ok(node.feature);
        }
        let index = usize::from(node.feature);
        if index >= features.len() {
            return Err(Error::Feature);
        }
        // At or below the threshold goes left, stated because the boundary is
        // where two implementations of one tree disagree and the gate compares
        // classes.
        at = if features[index] <= node.threshold {
            usize::from(node.left)
        } else {
            usize::from(node.right)
        };
    }
    Err(Error::Depth)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn constant_window(values: [i16; AXES]) -> [i16; WIN_N * AXES] {
        let mut out = [0_i16; WIN_N * AXES];
        for i in 0..WIN_N {
            for a in 0..AXES {
                out[i * AXES + a] = values[a];
            }
        }
        out
    }

    #[test]
    fn a_constant_window_is_exact_by_inspection() {
        let samples = constant_window([100, -100, 0]);
        let got = window(&samples, WIN_N, AXES).unwrap();
        assert_eq!(got.mean, [100, -100, 0]);
        assert_eq!(got.rms, [100, 100, 0]);
        assert_eq!(got.p2p, [0, 0, 0]);
    }

    /// The floored mean, on the window the oracle names after it.
    #[test]
    fn one_sample_of_minus_one_floors_to_minus_one() {
        let mut samples = constant_window([0, 0, 0]);
        samples[0] = -1;
        let got = window(&samples, WIN_N, AXES).unwrap();
        // A mean rounded to the nearest value would be zero here, and step 3
        // permits that reading too, which is why the rounding is pinned.
        assert_eq!(got.mean[0], -1);
        assert_eq!(got.p2p[0], 1);
    }

    /// The truncation at the top of the format, where the hand-written oracle
    /// expectation was wrong first: 4095 and not 4096, because the mean of the
    /// squares is 16776704 and 4096 squared is 16777216.
    #[test]
    fn the_root_mean_square_truncates_at_the_top_of_the_format() {
        let mut samples = constant_window([0, 0, 0]);
        samples[0] = -32768;
        samples[3] = 32767;
        let got = window(&samples, WIN_N, AXES).unwrap();
        assert_eq!(got.rms[0], 4095);
        assert_eq!(got.p2p[0], 32767);
        assert_eq!(got.mean[0], -1);
    }

    /// The corner this crate found. Every sample at negative full scale: the
    /// mean of the squares is 1073741824 and its root is exactly 32768, which
    /// Q15 stops one short of.
    #[test]
    fn the_root_mean_square_saturates_at_negative_full_scale() {
        let samples = constant_window([-32768, -32768, -32768]);
        let got = window(&samples, WIN_N, AXES).unwrap();
        assert_eq!(got.mean, [-32768, -32768, -32768]);
        // 32767 and not 32768, and not the negative 32768 a cast produces.
        assert_eq!(got.rms, [32767, 32767, 32767]);
        assert_eq!(got.p2p, [0, 0, 0]);
        // The unsaturated root, stated so the test shows what was saturated
        // rather than only the result.
        let squares = 128_u64 * 32768_u64 * 32768_u64;
        assert_eq!((squares >> 7).isqrt(), 32768);
    }

    #[test]
    fn a_window_of_the_wrong_shape_is_refused() {
        let samples = constant_window([0, 0, 0]);
        assert_eq!(window(&samples, 0, AXES), Err(Error::Argument));
        assert_eq!(window(&samples, 64, AXES), Err(Error::Shape));
        assert_eq!(window(&samples, WIN_N, 2), Err(Error::Shape));
    }

    // Two constructors rather than struct literals at each site. rustfmt's
    // struct_lit_width is 18, so a five field literal is broken one field per
    // line wherever it appears, and a tree written that way is forty lines of
    // punctuation. Named constructors keep every assert below short enough to
    // read and short enough that fn_call_width, which is 60, leaves it alone.
    // That width cost P18 a round trip on Monday 26 October 2026.
    fn branch(feature: u8, threshold: i16, left: u8, right: u8) -> Node {
        Node {
            feature,
            leaf: false,
            threshold,
            left,
            right,
        }
    }

    fn leaf(class: u8) -> Node {
        Node {
            feature: class,
            leaf: true,
            threshold: 0,
            left: 0,
            right: 0,
        }
    }

    #[test]
    fn a_tree_that_cycles_is_refused_at_the_depth_bound() {
        let cycle = [branch(0, 0, 1, 1), branch(0, 0, 0, 0)];
        let features = [0_i16; COUNT];
        assert_eq!(classify(&cycle, 8, &features), Err(Error::Depth));

        // The control: the same tree with a leaf terminates, so the refusal is
        // about the cycle and not about the tree being small.
        let fixed = [cycle[0], leaf(1)];
        assert_eq!(classify(&fixed, 8, &features), Ok(1));
    }

    #[test]
    fn the_tree_refusals_are_the_cs_refusals() {
        let features = [0_i16; COUNT];
        let one = [leaf(3)];
        let far = [branch(0, 0, 9, 9)];
        let wide = [branch(9, 0, 0, 0)];
        assert_eq!(classify(&[], 4, &features), Err(Error::Tree));
        assert_eq!(classify(&one, 0, &features), Err(Error::Depth));
        assert_eq!(classify(&far, 4, &features), Err(Error::Node));
        assert_eq!(classify(&wide, 4, &features), Err(Error::Feature));
    }
}
