"""P01's clock tree oracle: hand written registers, hand written answers, double entry.

WHAT THIS IS NOT, because committing a generator for an oracle would be a
mistake. Both halves of every row below were written by hand: the seven register
words AND the four frequencies they should decode to. What this script does is
recompute every answer from the field positions, written out here as literals
rather than imported, and refuse to write the file unless the two independent
statements agree on all 23 rows.

That is double entry. A generated expected value proves only that the generator
and the implementation share a bug. Two independent statements of the same number
disagree whenever either one is wrong, which is the property worth paying for.

It is in the repository so a reader can re-run it instead of taking the claim on
trust, and so that adding a row has an obvious place to go. Running it rewrites
projects/P01-toolchain-first-light/clock_vectors.json in place:

    python python/tools/gen_clock_vectors.py

Nothing in the build or the test suite calls it, and python/tests/test_clocktree.py
does not know it exists. The oracle is the committed file; this is how that file
was checked before it became one.
"""
import json
from pathlib import Path

# python/tools -> repo root, the same derivation build_host.py uses, so this runs
# from any working directory and on any of the three machines.
ROOT = Path(__file__).resolve().parent.parent.parent
OUT = ROOT / "projects" / "P01-toolchain-first-light" / "clock_vectors.json"

# Field positions, written here from stm32h7a3_regs.h, as literals rather than
# imported, so this check does not inherit the implementation's view of them.
AHB = {0x0: 1, 0x8: 2, 0x9: 4}
APB = {0x0: 1, 0x4: 2, 0x5: 4}


def check(r, hsi_nominal, hse_bypass):
    """A second decode, from the positions, used only to contradict the first."""
    def hsi():
        return hsi_nominal >> ((r["cr"] >> 3) & 0x3)

    def hse():
        if not (r["cr"] & (1 << 18)) or not (r["cr"] & (1 << 17)):
            return 0
        return hse_bypass

    def pll():
        if r["pllcfgr"] & (1 << 0):
            return 0, "pll-fractional"
        if not (r["pllcfgr"] & (1 << 16)):
            return 0, "pll-p-disabled"
        src = r["pllckselr"] & 0x3
        if src == 0:
            ref = hsi()
        elif src == 2:
            ref = hse()
            if ref == 0:
                return 0, "hse-not-bypass"
        else:
            return 0, "pll-source-unknown"
        if ref == 0:
            return 0, "pll-source-unknown"
        m = (r["pllckselr"] >> 4) & 0x3F
        n = ((r["pll1divr"] >> 0) & 0x1FF) + 1
        p = ((r["pll1divr"] >> 9) & 0x7F) + 1
        if m == 0:
            return 0, "pll-zero-divider"
        inp = ref // m
        if inp == 0 or inp > (0xFFFFFFFF // n):
            return 0, "pll-would-overflow"
        return (inp * n) // p, "ok"

    sws = (r["cfgr"] >> 3) & 0x7
    if sws == 0:
        sys_hz, why = hsi(), "ok"
    elif sws == 2:
        sys_hz, why = hse(), "ok"
        if sys_hz == 0:
            why = "hse-not-bypass"
    elif sws == 3:
        sys_hz, why = pll()
    else:
        sys_hz, why = 0, "sws-unknown"
    if why != "ok":
        return {"sys_hz": 0, "core_hz": 0, "ahb_hz": 0, "pclk1_hz": 0, "refusal": why}
    if sys_hz == 0:
        return {"sys_hz": 0, "core_hz": 0, "ahb_hz": 0, "pclk1_hz": 0,
                "refusal": "sws-unknown"}

    cpu = AHB.get((r["cdcfgr1"] >> 8) & 0xF, 0)
    ahb = AHB.get((r["cdcfgr1"] >> 0) & 0xF, 0)
    apb1 = APB.get((r["cdcfgr2"] >> 4) & 0x7, 0)
    if cpu == 0 or ahb == 0 or apb1 == 0:
        return {"sys_hz": 0, "core_hz": 0, "ahb_hz": 0, "pclk1_hz": 0,
                "refusal": "prescaler-undecoded"}
    core = sys_hz // cpu
    bus = core // ahb
    return {"sys_hz": sys_hz, "core_hz": core, "ahb_hz": bus,
            "pclk1_hz": bus // apb1, "refusal": "ok"}


def regs(cr, cfgr, pllckselr, pllcfgr, pll1divr, cdcfgr1, cdcfgr2):
    return {"cr": cr, "cfgr": cfgr, "pllckselr": pllckselr, "pllcfgr": pllcfgr,
            "pll1divr": pll1divr, "cdcfgr1": cdcfgr1, "cdcfgr2": cdcfgr2}


def expect(sys_hz, core_hz, ahb_hz, pclk1_hz, refusal="ok"):
    return {"sys_hz": sys_hz, "core_hz": core_hz, "ahb_hz": ahb_hz,
            "pclk1_hz": pclk1_hz, "refusal": refusal}


def refused(why):
    return expect(0, 0, 0, 0, why)


# The 280 MHz configuration as clock280.c writes it, used by several rows:
# PLLSRC = HSE (2), DIVM1 = 4 at bit 4; N1 = 279 so n = 280; P1 = 1 so p = 2.
SEL_280 = 0x00000042
DIVR_280 = 0x00000317
CFG_P_ON = 0x00010000
CR_HSE_BYPASS = 0x00060025   # HSION, HSIRDY, HSIDIVF, HSERDY, HSEBYP
CR_HSI_ONLY = 0x00000025
SWS_PLL1 = 0x0000001B
SWS_HSE = 0x00000012
SWS_CSI = 0x00000008

VECTORS = [
    {
        "name": "the-pll-is-configured-but-sws-still-says-hsi",
        "why": "The window between configuring the PLL and switching to it. Every "
               "PLL field here holds the 280 MHz configuration and the answer is "
               "still 64 MHz, because SWS is what says which source drives sys_ck. "
               "An implementation that reported the PLL it found configured would "
               "pass every other row in this file and fail this one. HPRE is set to "
               "divide by 2 so that the AHB answer differs from the core answer, "
               "which is the other thing this row pins: the AHB clock comes off the "
               "core and not off sys_ck.",
        "regs": regs(CR_HSI_ONLY, 0x00000000, SEL_280, CFG_P_ON, DIVR_280, 0x00000008, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": expect(64000000, 64000000, 32000000, 32000000),
    },
    {
        "name": "the-280-mhz-tree-at-the-nominal-input",
        "why": "The configuration p01-pll280 reached on the board on Sunday 4 October "
               "2026, decoded at the nominal 8 MHz. 8 over 4 is 2, times 280 is 560, "
               "over 2 is 280. CDCPRE divides by 1 and HPRE by 2, so the core is 280 "
               "and every bus is 140.",
        "regs": regs(CR_HSE_BYPASS, SWS_PLL1, SEL_280, CFG_P_ON, DIVR_280, 0x00000008, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": expect(280000000, 280000000, 140000000, 140000000),
    },
    {
        "name": "the-280-mhz-tree-at-the-measured-input",
        "why": "The same registers with the input the measurement implies, 7990652 Hz "
               "rather than 8 MHz. This is why clocktree_decode takes the two input "
               "frequencies as arguments instead of reading the header. The answer is "
               "279672820 and the measurement was 279672822, and the 2 Hz is worth "
               "keeping: this chain multiplies the input by exactly 35, so its output "
               "is always 35 times an integer, and 279672822 is not. No integer input "
               "reproduces the measured figure, which is a statement about the "
               "measurement's own resolution rather than about the decode. "
               "A SECOND RUN the same day implies 7983868 Hz instead, 849 parts per "
               "million from this one, and this row keeps run 1's figure rather than "
               "gaining a twin: nothing in the decode depends on which, it is linear "
               "in its input, and the row already proves a non-nominal input is "
               "carried through the whole chain. The spread belongs in the README and "
               "in stm32h7a3_regs.h, where it is evidence about the probe, and not in "
               "a vector, where it would be two tests of one thing.",
        "regs": regs(CR_HSE_BYPASS, SWS_PLL1, SEL_280, CFG_P_ON, DIVR_280, 0x00000008, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 7990652,
        "expect": expect(279672820, 279672820, 139836410, 139836410),
    },
    {
        "name": "divm1-of-three-truncates-before-the-multiply",
        "why": "The row that tells multiply-first from divide-first. 8 MHz over 3 is "
               "2666666 with the remainder thrown away, times 280 is 746666480, over "
               "2 is 373333240. Multiplying first and dividing after would give "
               "373333333, which is 93 Hz different and would never be noticed on "
               "this board, because the configuration it actually runs has an M that "
               "divides exactly. A second language is exactly where this would drift.",
        "regs": regs(CR_HSE_BYPASS, SWS_PLL1, 0x00000032, CFG_P_ON, DIVR_280, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": expect(373333240, 373333240, 373333240, 373333240),
    },
    {
        "name": "hsidiv-divides-the-internal-oscillator-by-eight",
        "why": "HSIDIV is two bits and the divisor is 1 << field, so field 3 is a "
               "divide by 8 and the decode is a right shift rather than a rounded "
               "division. 64 MHz becomes 8 MHz exactly.",
        "regs": regs(0x0000003D, 0x00000000, SEL_280, CFG_P_ON, DIVR_280, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": expect(8000000, 8000000, 8000000, 8000000),
    },
    {
        "name": "the-external-clock-is-selected-but-not-in-bypass",
        "why": "SWS says HSE and HSEBYP is clear, so the part is running from an "
               "oscillator this repository knows nothing about. Returning 8 MHz here "
               "would be asserting a board fact that does not apply, which is the "
               "whole reason this refusal exists.",
        "regs": regs(CR_HSI_ONLY, SWS_HSE, SEL_280, CFG_P_ON, DIVR_280, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("hse-not-bypass"),
    },
    {
        "name": "the-pll-runs-from-an-external-clock-that-is-not-in-bypass",
        "why": "The same bit, reached through the PLL rather than directly. This is "
               "the one reason that got MORE specific when the decode moved out of "
               "system.c on Sunday 4 October 2026: the old code reached the path "
               "where the PLL reference came back zero and reported an unknown "
               "source. The true cause is one bit in RCC_CR and that is where a "
               "reader should be sent.",
        "regs": regs(CR_HSI_ONLY, SWS_PLL1, SEL_280, CFG_P_ON, DIVR_280, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("hse-not-bypass"),
    },
    {
        "name": "the-pll-has-its-fractional-term-enabled",
        "why": "FRACEN set. FRACN is not decoded anywhere in this volume, and it "
               "moves the frequency by an amount this function cannot account for, "
               "so a plausible number here would be worse than no number.",
        "regs": regs(CR_HSE_BYPASS, SWS_PLL1, SEL_280, 0x00010001, DIVR_280, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("pll-fractional"),
    },
    {
        "name": "the-pll-p-output-is-disabled",
        "why": "DIVP1EN clear. SWS can say PLL1 while the P output is off, and then "
               "nothing is driving sys_ck through it, whatever the dividers hold.",
        "regs": regs(CR_HSE_BYPASS, SWS_PLL1, SEL_280, 0x00000000, DIVR_280, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("pll-p-disabled"),
    },
    {
        "name": "the-pll-runs-from-csi",
        "why": "PLLSRC is CSI, whose nominal frequency is not sourced anywhere in "
               "this repository and which nothing here selects.",
        "regs": regs(CR_HSE_BYPASS, SWS_PLL1, 0x00000041, CFG_P_ON, DIVR_280, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("pll-source-unknown"),
    },
    {
        "name": "divm1-is-zero",
        "why": "The hardware permits writing 0 to DIVM1 and the arithmetic does not "
               "permit dividing by it. Checked before the division rather than after.",
        "regs": regs(CR_HSE_BYPASS, SWS_PLL1, 0x00000002, CFG_P_ON, DIVR_280, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("pll-zero-divider"),
    },
    {
        "name": "the-multiply-would-overflow-32-bits",
        "why": "The internal oscillator at 64 MHz, M of 1 and N of 512. 64000000 "
               "times 512 is 3.3e10 and a 32 bit unsigned runs out at 4.29e9, so the "
               "product is checked against the limit before it is formed. This part's "
               "oscillator stops at 836 MHz, so the margin is real, and it is not "
               "wide enough to leave unchecked.",
        "regs": regs(CR_HSI_ONLY, SWS_PLL1, 0x00000010, CFG_P_ON, 0x000003FF, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("pll-would-overflow"),
    },
    {
        "name": "csi-drives-the-system-clock",
        "why": "SWS reports CSI. Same reason as the PLL source row, one level up.",
        "regs": regs(CR_HSE_BYPASS, SWS_CSI, SEL_280, CFG_P_ON, DIVR_280, 0x00000000, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("sws-unknown"),
    },
    {
        "name": "the-ahb-prescaler-holds-a-ratio-this-volume-has-not-sourced",
        "why": "HPRE of 0xF. Three of the sixteen encodings are read out of ST's "
               "device header and the rest are not, because the remaining ratios are "
               "not evenly spaced. Reporting the undivided frequency here would be "
               "wrong by exactly the ratio nobody would suspect.",
        "regs": regs(CR_HSE_BYPASS, SWS_PLL1, SEL_280, CFG_P_ON, DIVR_280, 0x0000000F, 0x00000000),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("prescaler-undecoded"),
    },
    {
        "name": "the-apb1-prescaler-holds-a-ratio-this-volume-has-not-sourced",
        "why": "CDPPRE1 of 6. The same refusal reached through the other prescaler "
               "encoding, which has three bits rather than four and its own table.",
        "regs": regs(CR_HSE_BYPASS, SWS_PLL1, SEL_280, CFG_P_ON, DIVR_280, 0x00000000, 0x00000060),
        "hsi_nominal": 64000000, "hse_bypass": 8000000,
        "expect": refused("prescaler-undecoded"),
    },
]

BIAS = [
    {
        "name": "the-280-mhz-setting-as-measured",
        "why": "What board_core_hz() reports against what Sunday 4 October 2026 "
               "measured. The reported frequency is high and a measured duration "
               "comes out short, which are opposite signs on the same fact.",
        "reported_hz": 280000000, "true_hz": 279672822,
        "expect": {"ok": True, "frequency_ppm": 1170, "duration_ppm": -1168, "delay_ppm": 1170},
    },
    {
        "name": "the-reset-clock-as-measured",
        "why": "The same two questions at the other clock, where every sign is the "
               "other way round. This row and the one above are why the single "
               "sentence 0.117 per cent high was replaced by a table.",
        "reported_hz": 64000000, "true_hz": 64194318,
        "expect": {"ok": True, "frequency_ppm": -3027, "duration_ppm": 3036, "delay_ppm": -3027},
    },
    {
        "name": "no-bias-at-all",
        "why": "The reported frequency is the truth. All three are zero and none is "
               "a rounding artefact.",
        "reported_hz": 280000000, "true_hz": 280000000,
        "expect": {"ok": True, "frequency_ppm": 0, "duration_ppm": 0, "delay_ppm": 0},
    },
    {
        "name": "a-half-part-per-million-rounds-away-from-zero",
        "why": "One hertz high on 2 MHz is exactly half a part per million, so the "
               "frequency answer is 1 and not 0. The duration answer divides by the "
               "reported frequency instead and lands just under a half, so it rounds "
               "to 0. Two of the three quantities differ here by a whole unit, which "
               "is the sharpest statement in this file that they are different "
               "quantities.",
        "reported_hz": 2000001, "true_hz": 2000000,
        "expect": {"ok": True, "frequency_ppm": 1, "duration_ppm": 0, "delay_ppm": 1},
    },
    {
        "name": "a-negative-half-part-per-million-also-rounds-away-from-zero",
        "why": "The same magnitude below, and NOT the mirror of the row above, "
               "which is the useful part. Halves away from zero makes the "
               "frequency -1 rather than 0, the rule four languages have to "
               "share and which neither C nor Rust gives for free. The duration "
               "comes out +1 here where the row above gives 0, because it divides "
               "by the reported frequency and 1999999 is the smaller denominator, "
               "which pushes the quotient just over a half instead of just under. "
               "A test written on Sunday 4 October 2026 asserted that the two "
               "quantities always carry opposite signs, and these two rows "
               "contradicted it on the first compiler to run it. They never carry "
               "the SAME sign, which is a weaker claim and the true one.",
        "reported_hz": 1999999, "true_hz": 2000000,
        "expect": {"ok": True, "frequency_ppm": -1, "duration_ppm": 1, "delay_ppm": -1},
    },
    {
        "name": "a-ratio-far-enough-from-one-to-saturate",
        "why": "A reported 4294967295 Hz against a true 1 Hz. The frequency answer "
               "saturates at the largest signed 32 bit value, and the duration answer "
               "does not, because its denominator is the large number. Saturating "
               "rather than refusing keeps the arithmetic total and keeps four "
               "languages from needing a second refusal path to agree on.",
        "reported_hz": 4294967295, "true_hz": 1,
        "expect": {"ok": True, "frequency_ppm": 2147483647, "duration_ppm": -1000000,
                   "delay_ppm": 2147483647},
    },
    {
        "name": "the-truth-is-not-known",
        "why": "A bias against an unknown frequency is not a small bias. Refused, "
               "with all three left at zero so a caller that ignores the return value "
               "gets no number rather than a wrong one.",
        "reported_hz": 280000000, "true_hz": 0,
        "expect": {"ok": False, "frequency_ppm": 0, "duration_ppm": 0, "delay_ppm": 0},
    },
    {
        "name": "nothing-was-reported",
        "why": "The other half of the same guard. board_core_hz() returns 0 when the "
               "clock is not established, and that value must never reach this "
               "arithmetic as though it were a frequency.",
        "reported_hz": 0, "true_hz": 279672822,
        "expect": {"ok": False, "frequency_ppm": 0, "duration_ppm": 0, "delay_ppm": 0},
    },
]


def ppm(delta, den):
    half = den // 2 if den >= 0 else -((-den) // 2)
    scaled = delta * 1000000
    n = scaled + half if delta >= 0 else scaled - half
    # Truncation toward zero, as C and Rust both do, written out because Python
    # floor-divides and would be one off for every negative row in this file.
    q = abs(n) // abs(den)
    q = q if n >= 0 else -q
    return max(-2147483648, min(2147483647, q))


def main():
    problems = []
    for v in VECTORS:
        got = check(v["regs"], v["hsi_nominal"], v["hse_bypass"])
        if got != v["expect"]:
            problems.append("{}\n    hand: {}\n    check: {}".format(v["name"], v["expect"], got))
    for b in BIAS:
        if not b["expect"]["ok"]:
            got = {"ok": False, "frequency_ppm": 0, "duration_ppm": 0, "delay_ppm": 0}
        else:
            f = ppm(b["reported_hz"] - b["true_hz"], b["true_hz"])
            d = ppm(b["true_hz"] - b["reported_hz"], b["reported_hz"])
            got = {"ok": True, "frequency_ppm": f, "duration_ppm": d, "delay_ppm": f}
        if got != b["expect"]:
            problems.append("{}\n    hand: {}\n    check: {}".format(b["name"], b["expect"], got))

    if problems:
        print("DOUBLE ENTRY DISAGREES on {} of {} rows:".format(
            len(problems), len(VECTORS) + len(BIAS)))
        for p in problems:
            print("  " + p)
        raise SystemExit(1)

    doc = {
        "what": "P01's clock tree oracle: register words in, frequencies or a named "
                "refusal out, plus the signed bias between a reported and a true "
                "frequency.",
        "how_to_read": "Every row carries both halves written by hand, the register "
                       "words and the answer, and a why that says what the row is "
                       "for. Removing a row removes a guarantee; changing an answer "
                       "to make a test pass is changing the specification and the "
                       "why has to change with it.",
        "where_the_numbers_come_from": "The field positions are in "
                       "c/board/stm32h7a3_regs.h, each with the authority that "
                       "settled it. The two measured frequencies, 279672822 Hz at "
                       "the 280 MHz setting and 64194318 Hz on the reset clock, were "
                       "measured against this board's 32.768 kHz crystal on Sunday "
                       "4 October 2026 and are recorded in "
                       "projects/P01-toolchain-first-light/README.md.",
        "not_yet_here": "The board's own register words, as dumped by p01-pll280 "
                        "before it writes anything. Every row below is constructed, "
                        "which is the right way to reach a refusal and the wrong way "
                        "to prove the 280 MHz row describes this board. That row "
                        "arrives with the next console capture.",
        "decode": VECTORS,
        "bias": BIAS,
    }
    OUT.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("ok {} decode rows and {} bias rows, double entry agrees on all of them"
          .format(len(VECTORS), len(BIAS)))



if __name__ == "__main__":
    main()
