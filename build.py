#!/usr/bin/env python3
"""Build the PDF and the single-file HTML edition of this volume.

    python build.py                     figures -> SVG, pdflatex x2, HTML
    python build.py --pdf               PDF only
    python build.py --html              figures + HTML only
    python build.py --figures           figures only (latex + dvisvgm)
    python build.py --chapter 7         one chapter, its own PDF and HTML
    python build.py --chapters          every chapter, one file each
    python build.py --drift ../<sibling>/build.py
                                        prove the two toolchain copies are identical
                                        outside their DOC blocks
    python build.py --check sections/<pfx>07.tex
                                        compile that section alone (pdflatex), render
                                        its figures to SVG, run the HTML converter on it
                                        and report unknown macros / non-ASCII in code

Everything that names the volume lives in the DOC block below, and the file is
byte-identical to its siblings outside it. The section file prefix, the unit
word, the metadata labels and the accent colours are all read from there, which
is what lets one converter serve every volume and what --drift polices.

The HTML converter understands the LaTeX subset defined in main.tex (see the
authoring macros there).  Unknown commands are dropped and reported.
"""
import base64
import html
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
FIGBUILD = BUILD / "fig"
FIGDIR = ROOT / "figures"
SECTIONS = ROOT / "sections"
MAIN = ROOT / "main.tex"
# ---------------------------------------------------------------- volume identity
# Everything that names this volume lives in DOC. Below this block build.py must
# stay byte-identical to the sibling volumes, which
#     python build.py --drift ../EmbeddedLinux_Top20/build.py
# proves. DOC itself is skipped by that check, so keep it flat and short.
DOC = {
    "slug":       "firmware-nucleo-h7",
    "title":      "Twenty Firmware Projects on the NUCLEO-H7A3ZI-Q",
    "subtitle":   "from the Bench Inventory",
    "author":     "Joseph Ambrose Pagaran",
    "date":       "Sunday 20 September 2026",
    "unit":       "Chapter",        # the word printed before the number
    "label_pfx":  "sec:c",          # anchors and \label; must match main.tex
    "meta1":      "Target board",   # \project argument 3 label
    "meta2":      "Theme",          # \project argument 4 label
    "keyfacts":   "Key facts",      # title of the key-facts box
    "code_cols":  96,               # see AUTHORING.md, "Line lengths"
    "ascii_cols": 112,
    "accent":     {"light": "#7a3b8f", "dark": "#c79fe0"},
    "accent_bg":  {"light": "#f3edf8", "dark": "#261c2e"},
    "kf_line":    {"light": "#6a2f80", "dark": "#b587cf"},
}


if ROOT.name != DOC["slug"]:
    raise SystemExit(
        f"build.py: DOC['slug'] is {DOC['slug']!r} but this file lives in "
        f"{ROOT.name!r}. Update the DOC block after copying the toolchain.")

PDF_NAME = DOC["slug"] + ".pdf"
HTML_NAME = DOC["slug"] + ".html"

VERBATIM_ENVS = ["asciiart", "ccode", "cppcode", "rustcode", "shellcode", "pycode",
                 "makecode", "plaincode", "dtscode", "yamlcode", "asmcode", "ldcode",
                 "lstlisting", "verbatim"]

WARNINGS = []


def warn(msg):
    WARNINGS.append(msg)


def run(cmd, cwd=ROOT, timeout=600):
    return subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True,
                          errors="replace", timeout=timeout)


# ============================================================ figures -> SVG

STANDALONE = r"""\documentclass[dvisvgm,border=5pt]{standalone}
\usepackage[T1]{fontenc}
\usepackage{lmodern}
\usepackage{xcolor}
\usepackage{tikz}
\usepackage[european]{circuitikz}
\input{tikz_preamble.tex}
\begin{document}
\input{figures/%s.tex}
\end{document}
"""


FILE_LINE_ERR = re.compile(r"^[^\s:]+\.(?:tex|sty|cls|def):\d+: ")


def tex_errors(logtext):
    """Both plain '! Undefined control sequence.' and the -file-line-error form
    'sections/p07.tex:42: Undefined control sequence.' count as errors."""
    out, lines = [], logtext.splitlines()
    for i, ln in enumerate(lines):
        if ln.startswith("!") or FILE_LINE_ERR.match(ln):
            out.append("\n".join(lines[i:i + 4]))
    return out


def build_figure(name, force=False):
    """latex -> dvi -> svg for figures/<name>.tex. Returns path to svg or None."""
    FIGBUILD.mkdir(parents=True, exist_ok=True)
    src = FIGDIR / f"{name}.tex"
    svg = FIGBUILD / f"{name}.svg"
    if not src.exists():
        warn(f"figure missing: figures/{name}.tex")
        return None
    if svg.exists() and not force and svg.stat().st_mtime > src.stat().st_mtime \
            and svg.stat().st_mtime > (ROOT / "tikz_preamble.tex").stat().st_mtime:
        return svg
    wrapper = FIGBUILD / f"{name}.tex"
    wrapper.write_text(STANDALONE % name, encoding="utf-8")
    r = run(["latex", "-interaction=nonstopmode", "-halt-on-error",
             f"-output-directory={FIGBUILD.relative_to(ROOT).as_posix()}",
             wrapper.relative_to(ROOT).as_posix()])
    log = FIGBUILD / f"{name}.log"
    dvi = FIGBUILD / f"{name}.dvi"
    if r.returncode != 0 or not dvi.exists():
        errs = tex_errors(log.read_text(encoding="utf-8", errors="replace")) if log.exists() else [r.stdout[-800:]]
        warn(f"figure FAILED: figures/{name}.tex\n" + "\n".join(errs[:3]))
        return None
    # --no-fonts converts glyphs to paths: the figure then looks exactly like the
    # PDF in every browser, where an embedded font is re-kerned by the font engine
    # and opens visible gaps inside words.
    r = run(["dvisvgm", "--no-fonts", "--exact-bbox", "--optimize", "--relative",
             "--precision=3", "-o", svg.as_posix(), dvi.as_posix()])
    if r.returncode != 0 or not svg.exists():
        warn(f"dvisvgm FAILED: {name}\n{r.stderr[-500:]}")
        return None
    return svg


def all_figure_names(text):
    return re.findall(r"\\diagram\{([^}]+)\}", text)


# ============================================================ PDF

def build_pdf():
    BUILD.mkdir(exist_ok=True)
    ok = True
    for _ in range(3):
        r = run(["pdflatex", "-interaction=nonstopmode", "-file-line-error",
                 "-output-directory=build", "main.tex"], timeout=1200)
        if r.returncode != 0:
            ok = False
            break
        ok = True
    log = BUILD / "main.log"
    errs = tex_errors(log.read_text(encoding="utf-8", errors="replace")) if log.exists() else []
    if errs:
        print("pdflatex errors:")
        for e in errs[:10]:
            print(e)
    pdf = BUILD / "main.pdf"
    if pdf.exists():
        shutil.copyfile(pdf, ROOT / PDF_NAME)
        print(f"PDF written: {PDF_NAME}")
    return ok and not errs


# ============================================================ LaTeX tokenizer

class Node:
    __slots__ = ("kind", "name", "args", "opt", "body", "text")

    def __init__(self, kind, name=None, args=None, opt=None, body=None, text=""):
        self.kind, self.name, self.args, self.opt, self.body, self.text = kind, name, args or [], opt, body, text

    def __repr__(self):
        return f"<{self.kind} {self.name or self.text[:20]!r}>"


CMD_ARGS = {
    "project": 4, "diagram": 2, "section": 1, "subsection": 1, "subsubsection": 1, "paragraph": 1,
    "part": 1, "chapter": 1, "textbf": 1, "emph": 1, "textit": 1, "texttt": 1, "textsf": 1, "textsc": 1,
    "underline": 1, "url": 1, "href": 2, "ref": 1, "label": 1, "caption": 1, "multicolumn": 3,
    "textcolor": 2, "color": 1, "footnote": 1, "hspace": 1, "vspace": 1, "includegraphics": 1,
    "input": 1, "mbox": 1, "text": 1, "pubdate": 1, "cmidrule": 1, "setlength": 2, "rule": 2, "pageref": 1,
    "nameref": 1, "autoref": 1, "nolinkurl": 1, "texorpdfstring": 2, "addcontentsline": 3,
    "usebox": 1, "renewcommand": 2, "newcommand": 2, "parbox": 2, "makebox": 1, "textsuperscript": 1,
    "textsubscript": 1, "hyperref": 1, "fbox": 1, "path": 1, "textbackslash": 0, "thispagestyle": 1,
    "pagestyle": 1, "definecolor": 3, "sbox": 2, "resizebox": 3, "phantom": 1, "st": 1, "hl": 1,
    "lstset": 1, "captionof": 2, "marginpar": 1, "enlargethispage": 1, "columnbreak": 0,
}
OPT_CMDS = {"section", "subsection", "subsubsection", "paragraph", "part", "chapter", "item",
            "includegraphics", "rule", "parbox", "makebox", "hspace", "vspace", "sqrt", "newcommand",
            "renewcommand", "captionof", "linebreak", "nolinebreak", "footnote", "cmidrule"}
ENV_ARGS = {"note": 1, "tabular": 1, "tabularx": 2, "minipage": 1, "wrapfigure": 2, "tcolorbox": 0,
            "multicols": 1, "longtable": 1}
SIMPLE = {
    "ldots": "\u2026", "dots": "\u2026", "textmu": "\u00b5", "textdegree": "\u00b0", "textohm": "\u03a9",
    "texttimes": "\u00d7", "textasciitilde": "~", "textasciicircum": "^", "textless": "<",
    "textgreater": ">", "textbackslash": "\\", "quad": "\u2003", "qquad": "\u2003\u2003",
    "today": DOC["date"], "LaTeX": "LaTeX", "TeX": "TeX", "textregistered": "\u00ae",
    "textendash": "-", "textemdash": "-", "newline": "<br>", "linebreak": "<br>", "textbullet": "\u2022",
    "textpm": "\u00b1", "textquotedblleft": "\u201c", "textquotedblright": "\u201d", "slash": "/",
    "textvisiblespace": "\u2423", "copyright": "\u00a9", "textcopyright": "\u00a9", "S": "\u00a7",
    "P": "\u00b6", "dag": "\u2020", "checkmark": "\u2713",
}
IGNORE = {"clearpage", "newpage", "noindent", "medskip", "bigskip", "smallskip", "centering",
          "raggedright", "raggedleft", "small", "footnotesize", "scriptsize", "tiny", "normalsize",
          "large", "Large", "LARGE", "huge", "Huge", "sffamily", "ttfamily", "rmfamily", "bfseries",
          "itshape", "toprule", "midrule", "bottomrule", "hline", "hfill", "vfill", "maketitle",
          "thispagestyle", "pagestyle", "setlength", "renewcommand", "newcommand", "definecolor",
          "phantomsection", "nolinebreak", "relax", "frenchspacing", "appendix", "printindex",
          "sbox", "lstset", "enlargethispage", "columnbreak", "hspace", "vspace", "rule",
          "addcontentsline", "pagebreak", "nopagebreak", "allowbreak", "indent", "par", "label",
          "protect", "selectfont", "normalfont", "textnormal", "hyphenation", "noindent", "leavevmode",
          "strut", "vphantom", "hphantom", "phantom", "smash", "arraystretch", "tabcolsep"}


class Tokenizer:
    def __init__(self, text):
        self.s = text
        self.i = 0
        self.n = len(text)

    def parse(self, end_env=None):
        nodes = []
        s, n = self.s, self.n
        buf = []

        def flush():
            if buf:
                nodes.append(Node("text", text="".join(buf)))
                buf.clear()

        while self.i < n:
            c = s[self.i]
            if c == "\\":
                m = re.match(r"\\([A-Za-z]+\*?)", s[self.i:])
                if m:
                    name = m.group(1)
                    self.i += m.end()
                    if name == "begin":
                        flush()
                        envname = self.read_group()
                        node = self.parse_env(envname)
                        nodes.append(node)
                        continue
                    if name == "end":
                        envname = self.read_group()
                        if envname != end_env:
                            warn(f"mismatched \\end{{{envname}}} (expected {end_env})")
                        flush()
                        return nodes
                    if name in ("verb", "verb*", "lstinline"):
                        flush()
                        if self.i < n:
                            d = s[self.i]
                            j = s.find(d, self.i + 1)
                            if j < 0:
                                j = self.i + 1
                            nodes.append(Node("verb", text=s[self.i + 1:j]))
                            self.i = j + 1
                        continue
                    self.skip_ws_after_control_word()
                    base = name.rstrip("*")
                    opt = None
                    if base in OPT_CMDS:
                        opt = self.read_opt()
                    nargs = CMD_ARGS.get(base, 0)
                    args = []
                    for _ in range(nargs):
                        args.append(self.read_group_nodes(base))
                    flush()
                    nodes.append(Node("cmd", name=base, args=args, opt=opt))
                    continue
                # control symbol
                self.i += 1
                if self.i >= n:
                    break
                c2 = s[self.i]
                self.i += 1
                if c2 == "\\":
                    flush()
                    self.read_opt()
                    nodes.append(Node("cmd", name="\\\\"))
                elif c2 in "&%_#$&{}":
                    buf.append(c2)
                elif c2 == ",":
                    buf.append("\u2009")
                elif c2 == ";":
                    buf.append(" ")
                elif c2 == " " or c2 == "\n":
                    buf.append(" ")
                elif c2 == "-":
                    pass
                elif c2 == "~":
                    buf.append("~")
                elif c2 == "'" or c2 == "`" or c2 == '"' or c2 == "^":
                    # accent: apply to next char crudely
                    if self.i < n:
                        buf.append(s[self.i])
                        self.i += 1
                elif c2 == "(":
                    flush()
                    j = s.find("\\)", self.i)
                    nodes.append(Node("math", text=s[self.i:j]))
                    self.i = j + 2
                elif c2 == "[":
                    flush()
                    j = s.find("\\]", self.i)
                    nodes.append(Node("dmath", text=s[self.i:j]))
                    self.i = j + 2
                elif c2 == "@" or c2 == "/":
                    pass
                else:
                    buf.append(c2)
            elif c == "{":
                flush()
                self.i += 1
                inner = self.parse(end_env="__group__")
                nodes.append(Node("group", body=inner))
            elif c == "}":
                self.i += 1
                flush()
                if end_env == "__group__":
                    return nodes
                warn("stray }")
            elif c == "$":
                flush()
                if s.startswith("$$", self.i):
                    j = s.find("$$", self.i + 2)
                    nodes.append(Node("dmath", text=s[self.i + 2:j]))
                    self.i = j + 2
                else:
                    j = s.find("$", self.i + 1)
                    if j < 0:
                        j = n
                    nodes.append(Node("math", text=s[self.i + 1:j]))
                    self.i = j + 1
            elif c == "&":
                flush()
                nodes.append(Node("amp"))
                self.i += 1
            elif c == "~":
                buf.append("\u00a0")
                self.i += 1
            elif c == "\n":
                m = re.match(r"\n[ \t]*\n[\s]*", s[self.i:])
                if m:
                    flush()
                    nodes.append(Node("par"))
                    self.i += m.end()
                else:
                    buf.append(" ")
                    self.i += 1
            elif c == "\x00":
                flush()
                m = re.match(r"\x00(\d+)\x00", s[self.i:])
                nodes.append(Node("block", text=m.group(1)))
                self.i += m.end()
            else:
                buf.append(c)
                self.i += 1
        flush()
        return nodes

    def skip_ws_after_control_word(self):
        while self.i < self.n and self.s[self.i] in " \t":
            self.i += 1
        if self.i < self.n and self.s[self.i] == "\n":
            # one newline is like a space, but a blank line must survive as a paragraph break
            m = re.match(r"\n[ \t]*\n", self.s[self.i:])
            if not m:
                self.i += 1

    def read_group(self):
        while self.i < self.n and self.s[self.i] in " \t\n":
            self.i += 1
        if self.i < self.n and self.s[self.i] == "{":
            depth, j = 0, self.i
            while j < self.n:
                if self.s[j] == "{":
                    depth += 1
                elif self.s[j] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            g = self.s[self.i + 1:j]
            self.i = j + 1
            return g
        return ""

    def read_group_nodes(self, cmd):
        while self.i < self.n and self.s[self.i] in " \t":
            self.i += 1
        if self.i < self.n and self.s[self.i] == "{":
            self.i += 1
            return self.parse(end_env="__group__")
        # single token argument
        if self.i < self.n:
            ch = self.s[self.i]
            self.i += 1
            return [Node("text", text=ch)]
        return []

    def read_opt(self):
        j = self.i
        while j < self.n and self.s[j] in " \t":
            j += 1
        if j < self.n and self.s[j] == "[":
            depth, k = 0, j
            while k < self.n:
                if self.s[k] == "[":
                    depth += 1
                elif self.s[k] == "]":
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            opt = self.s[j + 1:k]
            self.i = k + 1
            return opt
        return None

    def parse_env(self, envname):
        opt = self.read_opt()
        args = []
        for _ in range(ENV_ARGS.get(envname, 0)):
            args.append(self.read_group_nodes(envname))
        body = self.parse(end_env=envname)
        return Node("env", name=envname, args=args, opt=opt, body=body)


# ============================================================ HTML renderer

MATH_MAP = [
    (r"\mu", "\u00b5"), (r"\Omega", "\u03a9"), (r"\pm", "\u00b1"), (r"\times", "\u00d7"),
    (r"\leq", "\u2264"), (r"\geq", "\u2265"), (r"\le", "\u2264"), (r"\ge", "\u2265"),
    (r"\approx", "\u2248"), (r"\rightarrow", "\u2192"), (r"\leftarrow", "\u2190"), (r"\to", "\u2192"),
    (r"\Rightarrow", "\u21d2"), (r"\Leftarrow", "\u21d0"), (r"\leftrightarrow", "\u2194"),
    (r"\cdot", "\u00b7"), (r"\sim", "~"), (r"\circ", "\u00b0"), (r"\infty", "\u221e"),
    (r"\ldots", "\u2026"), (r"\dots", "\u2026"), (r"\neq", "\u2260"), (r"\ne", "\u2260"),
    (r"\alpha", "\u03b1"), (r"\beta", "\u03b2"), (r"\gamma", "\u03b3"), (r"\delta", "\u03b4"),
    (r"\Delta", "\u0394"), (r"\theta", "\u03b8"), (r"\lambda", "\u03bb"), (r"\pi", "\u03c0"),
    (r"\sigma", "\u03c3"), (r"\tau", "\u03c4"), (r"\omega", "\u03c9"), (r"\phi", "\u03c6"),
    (r"\epsilon", "\u03b5"), (r"\rho", "\u03c1"), (r"\eta", "\u03b7"), (r"\sum", "\u2211"),
    (r"\int", "\u222b"), (r"\partial", "\u2202"), (r"\nabla", "\u2207"), (r"\propto", "\u221d"),
    (r"\ll", "\u226a"), (r"\gg", "\u226b"), (r"\,", "\u2009"), (r"\;", " "), (r"\!", ""), (r"\ ", " "),
    (r"\left", ""), (r"\right", ""), (r"\%", "%"), (r"\_", "_"), (r"\&", "&"), (r"\#", "#"),
]


MATH_OPS = ("arctan", "arcsin", "arccos", "atan", "tan", "sin", "cos",
            "log", "ln", "exp", "max", "min", "abs")


def render_math(t):
    t = t.strip()
    for op in MATH_OPS:
        t = re.sub(r"\\" + op + r"\b", op, t)
    t = re.sub(r"\\(?:mathrm|text|textrm|mathit|mathbf|operatorname)\{([^}]*)\}", r"\1", t)
    t = re.sub(r"\\frac\{([^}]*)\}\{([^}]*)\}", r"(\1)/(\2)", t)
    t = re.sub(r"\\sqrt\{([^}]*)\}", "\u221a(\\1)", t)
    for k, v in sorted(MATH_MAP, key=lambda kv: -len(kv[0])):
        t = t.replace(k, v)
    t = html.escape(t, quote=False)
    t = re.sub(r"\^\{([^}]*)\}", r"<sup>\1</sup>", t)
    t = re.sub(r"\^(\S)", r"<sup>\1</sup>", t)
    t = re.sub(r"_\{([^}]*)\}", r"<sub>\1</sub>", t)
    t = re.sub(r"_(\S)", r"<sub>\1</sub>", t)
    t = t.replace("{", "").replace("}", "")
    return f'<span class="math">{t}</span>'


class Renderer:
    def __init__(self, blocks, figures):
        self.blocks = blocks          # placeholder id -> (env, code)
        self.figures = figures        # name -> svg data uri (or None)
        self.toc = []                 # (level, id, text)
        self.fig_no = 0
        self.tab_no = 0
        self.sec_no = 0
        self.labels = {}
        self.pending_label = None
        self.title = ""

    # -------------------------------------------------- inline
    def inline(self, nodes):
        out = []
        for nd in nodes:
            out.append(self.inline_node(nd))
        return "".join(out)

    def text_html(self, t):
        t = html.escape(t, quote=False)
        t = t.replace("``", "\u201c").replace("''", "\u201d").replace("---", "-").replace("--", "-")
        t = re.sub(r"(?<![\w])`", "\u2018", t)
        t = re.sub(r"'(?=[\s.,;:!?)]|$)", "\u2019", t)
        return t

    def inline_node(self, nd):
        k = nd.kind
        if k == "text":
            return self.text_html(nd.text)
        if k == "group":
            return self.inline(nd.body)
        if k == "verb":
            return f"<code>{html.escape(nd.text)}</code>"
        if k == "math":
            return render_math(nd.text)
        if k == "dmath":
            return f'<div class="dmath">{render_math(nd.text)}</div>'
        if k == "amp":
            return "&amp;"
        if k == "par":
            return " "
        if k == "block":
            return self.block_code(nd.text)
        if k == "env":
            return self.env(nd)      # block env inside inline context
        if k == "cmd":
            return self.cmd(nd)
        return ""

    def cmd(self, nd):
        n, a = nd.name, nd.args
        if n in SIMPLE:
            return SIMPLE[n]
        if n == "\\\\":
            return "<br>"
        if n in ("textbf",):
            return f"<strong>{self.inline(a[0])}</strong>"
        if n in ("emph", "textit"):
            return f"<em>{self.inline(a[0])}</em>"
        if n == "texttt" or n == "path" or n == "nolinkurl":
            return f"<code>{self.inline(a[0])}</code>"
        if n == "textsf":
            return f'<span class="sf">{self.inline(a[0])}</span>'
        if n == "textsc":
            return f'<span class="sc">{self.inline(a[0])}</span>'
        if n == "underline":
            return f"<u>{self.inline(a[0])}</u>"
        if n == "textsuperscript":
            return f"<sup>{self.inline(a[0])}</sup>"
        if n == "textsubscript":
            return f"<sub>{self.inline(a[0])}</sub>"
        if n == "url":
            u = plain(a[0])
            return f'<a href="{html.escape(u)}">{html.escape(u)}</a>'
        if n == "href":
            u = plain(a[0])
            return f'<a href="{html.escape(u)}">{self.inline(a[1])}</a>'
        if n == "ref" or n == "autoref" or n == "nameref":
            lab = plain(a[0])
            txt = self.labels.get(lab)
            if txt is None:
                m = re.match(re.escape(DOC["label_pfx"]) + r"(\d+)", lab)
                if m:
                    txt = f'{DOC["unit"]} {int(m.group(1))}'
                elif lab.startswith("fig:"):
                    txt = "figure"
                elif lab.startswith("tab:"):
                    txt = "table"
                else:
                    txt = lab
            return f'<a href="#{html.escape(lab)}">{html.escape(txt)}</a>'
        if n == "pageref":
            return ""
        if n == "label":
            self.pending_label = plain(a[0])
            return f'<span id="{html.escape(plain(a[0]))}"></span>'
        if n == "footnote":
            return f' <span class="fn">({self.inline(a[0])})</span>'
        if n == "textcolor":
            return f'<span style="color:{plain(a[0])}">{self.inline(a[1])}</span>'
        if n in ("mbox", "text", "pubdate", "hyperref", "fbox", "makebox", "texorpdfstring", "hl", "st"):
            return self.inline(a[0]) if a else ""
        if n == "parbox":
            return self.inline(a[1])
        if n == "resizebox":
            return self.inline(a[2])
        if n == "multicolumn":
            return self.inline(a[2])
        if n == "includegraphics":
            return f'<span class="missing">[graphic {html.escape(plain(a[0]))}]</span>'
        if n == "caption":
            return ""
        if n in ("section", "subsection", "subsubsection", "paragraph", "part", "project", "diagram",
                 "tableofcontents", "item"):
            return self.block_cmd(nd)
        if n in IGNORE:
            return ""
        warn(f"unknown command \\{n}")
        return ""

    # -------------------------------------------------- blocks
    def blocks_html(self, nodes):
        """Render a node list as block content: paragraphs + block elements."""
        out, para = [], []

        def flush():
            s = "".join(para).strip()
            if s:
                out.append(f"<p>{s}</p>")
            para.clear()

        for nd in nodes:
            if nd.kind == "par":
                flush()
            elif nd.kind == "block":
                flush()
                out.append(self.block_code(nd.text))
            elif nd.kind == "env" and nd.name not in ("group",):
                flush()
                out.append(self.env(nd))
            elif nd.kind == "cmd" and nd.name in ("section", "subsection", "subsubsection", "paragraph",
                                                  "part", "project", "diagram", "tableofcontents", "item",
                                                  "maketitle"):
                if nd.name == "paragraph":
                    flush()
                    para.append(f"<strong class=\"runin\">{self.inline(nd.args[0])}</strong> ")
                else:
                    flush()
                    out.append(self.block_cmd(nd))
            elif nd.kind == "dmath":
                flush()
                out.append(f'<div class="dmath">{render_math(nd.text)}</div>')
            else:
                para.append(self.inline_node(nd))
        flush()
        return "\n".join(out)

    def heading(self, level, text, cls="", anchor=None, toc=True, numbered=False):
        self.sec_no += 1
        hid = anchor or f"h{self.sec_no}"
        if toc:
            self.toc.append((level, hid, re.sub(r"<[^>]+>", "", text)))
        c = f' class="{cls}"' if cls else ""
        return f'<h{level} id="{hid}"{c}>{text}</h{level}>'

    def block_cmd(self, nd):
        n, a = nd.name, nd.args
        if n == "part":
            return self.heading(1, self.inline(a[0]), cls="part")
        if n == "section":
            return self.heading(2, self.inline(a[0]))
        if n == "subsection":
            return self.heading(3, self.inline(a[0]), toc=False)
        if n == "subsubsection":
            return self.heading(4, self.inline(a[0]), toc=False)
        if n == "paragraph":
            return f'<p><strong class="runin">{self.inline(a[0])}</strong></p>'
        if n == "project":
            num = plain(a[0])
            title = self.inline(a[1])
            hid = f'{DOC["label_pfx"]}{num}'
            label = f'{DOC["unit"]} {int(num)}'
            self.labels[hid] = label
            h = self.heading(2, f'<span class="pnum">{label}</span> {title}', cls="project", anchor=hid)
            meta = (f'<p class="meta"><span>{DOC["meta1"]}: {self.inline(a[2])}</span>'
                    f'<span>{DOC["meta2"]}: {self.inline(a[3])}</span></p>')
            return h + "\n" + meta
        if n == "diagram":
            name = plain(a[0])
            self.fig_no += 1
            cap = self.inline(a[1])
            markup = self.figures.get(name)
            if markup:
                img = f'<div class="svgwrap" role="img" aria-label="{html.escape(re.sub(r"<[^>]+>", "", cap), quote=True)}">{markup}</div>'
            else:
                img = f'<div class="missing">figure {html.escape(name)} did not render</div>'
            self.labels[f"fig:{name}"] = f"Figure {self.fig_no}"
            return (f'<figure class="diagram" id="fig:{html.escape(name)}">{img}'
                    f'<figcaption><b>Figure {self.fig_no}.</b> {cap}</figcaption></figure>')
        if n == "tableofcontents":
            return "<!--TOC-->"
        if n == "item":
            return ""   # handled by list rendering
        if n == "maketitle":
            return "<!--TITLE-->"
        return ""

    def block_code(self, bid):
        env, code = self.blocks[bid]
        code = html.escape(code.strip("\n"))
        if env == "asciiart":
            return f'<pre class="ascii">{code}</pre>'
        lang = {"ccode": "c", "cppcode": "cpp", "rustcode": "rust", "shellcode": "bash",
                "pycode": "python", "makecode": "make", "dtscode": "dts",
                "yamlcode": "yaml", "asmcode": "armasm", "ldcode": "text"}.get(env, "text")
        return f'<pre class="code"><code class="lang-{lang}">{code}</code></pre>'

    def split_items(self, nodes):
        items, cur, label = [], None, None
        pre = []
        for nd in nodes:
            if nd.kind == "cmd" and nd.name == "item":
                if cur is not None:
                    items.append((label, cur))
                cur, label = [], nd.opt
            elif cur is None:
                pre.append(nd)
            else:
                cur.append(nd)
        if cur is not None:
            items.append((label, cur))
        return items

    def render_list(self, tag, nodes, cls=""):
        items = self.split_items(nodes)
        c = f' class="{cls}"' if cls else ""
        out = [f"<{tag}{c}>"]
        for label, body in items:
            inner = self.blocks_html(body)
            # unwrap a single paragraph for compactness
            if inner.count("<p>") == 1 and inner.startswith("<p>") and inner.endswith("</p>"):
                inner = inner[3:-4]
            if label is not None:
                lab = self.inline(Tokenizer(label).parse("__none__"))
                out.append(f"<li><strong>{lab}</strong> {inner}</li>")
            else:
                out.append(f"<li>{inner}</li>")
        out.append(f"</{tag}>")
        return "\n".join(out)

    def render_dl(self, nodes, cls=""):
        items = self.split_items(nodes)
        c = f' class="{cls}"' if cls else ""
        out = [f"<dl{c}>"]
        for label, body in items:
            inner = self.blocks_html(body)
            if inner.count("<p>") == 1 and inner.startswith("<p>") and inner.endswith("</p>"):
                inner = inner[3:-4]
            lab = self.inline(Tokenizer(label or "").parse("__none__"))
            out.append(f"<dt>{lab}</dt><dd>{inner}</dd>")
        out.append("</dl>")
        return "\n".join(out)

    def render_table(self, nd):
        rows, cur = [], []
        for x in nd.body:
            if x.kind == "cmd" and x.name == "\\\\":
                rows.append(cur)
                cur = []
            else:
                cur.append(x)
        if any(y.kind != "text" or y.text.strip() for y in cur):
            rows.append(cur)
        # rows[0:header_rows] are header rows: the \midrule token opens the row after them
        header_rows = 0
        for i, r in enumerate(rows):
            if any(y.kind == "cmd" and y.name == "midrule" for y in r):
                header_rows = i
                break
        table = ["<table>"]
        for i, r in enumerate(rows):
            cells, cell = [], []
            for y in r:
                if y.kind == "amp":
                    cells.append(cell)
                    cell = []
                elif y.kind == "cmd" and y.name in ("toprule", "midrule", "bottomrule", "hline", "cmidrule"):
                    continue
                else:
                    cell.append(y)
            cells.append(cell)
            if all(not self.inline(c).strip() for c in cells):
                continue
            tag = "th" if i < header_rows else "td"
            tds = []
            for c in cells:
                span = ""
                for y in c:
                    if y.kind == "cmd" and y.name == "multicolumn":
                        span = f' colspan="{plain(y.args[0])}"'
                tds.append(f"<{tag}{span}>{self.blocks_html(c) if any(z.kind=='block' for z in c) else self.inline(c).strip()}</{tag}>")
            table.append("<tr>" + "".join(tds) + "</tr>")
        table.append("</table>")
        return "\n".join(table)

    def env(self, nd):
        n = nd.name
        if n in ("itemize",):
            return self.render_list("ul", nd.body)
        if n == "enumerate":
            return self.render_list("ol", nd.body)
        if n == "steps":
            return self.render_list("ol", nd.body, cls="steps")
        if n == "description":
            return self.render_dl(nd.body)
        if n == "keyfacts":
            return ('<div class="keyfacts"><div class="kf-title">'
                    + html.escape(DOC["keyfacts"]) + '</div>'
                    + self.render_dl(nd.body, cls="kf") + "</div>")
        if n == "note":
            title = self.inline(nd.args[0]) if nd.args else "Note"
            return f'<aside class="note"><div class="note-title">{title}</div>{self.blocks_html(nd.body)}</aside>'
        if n in ("tabular", "tabularx", "longtable"):
            return '<div class="tablewrap">' + self.render_table(nd) + "</div>"
        if n == "table":
            cap, lab = None, None
            inner = []
            for x in nd.body:
                if x.kind == "cmd" and x.name == "caption":
                    cap = self.inline(x.args[0])
                elif x.kind == "cmd" and x.name == "label":
                    lab = plain(x.args[0])
                else:
                    inner.append(x)
            self.tab_no += 1
            if lab:
                self.labels[lab] = f"Table {self.tab_no}"
            idattr = f' id="{html.escape(lab)}"' if lab else ""
            capt = f'<figcaption><b>Table {self.tab_no}.</b> {cap}</figcaption>' if cap else ""
            return f'<figure class="table"{idattr}>{capt}{self.blocks_html(inner)}</figure>'
        if n == "figure":
            cap = None
            inner = []
            for x in nd.body:
                if x.kind == "cmd" and x.name == "caption":
                    cap = self.inline(x.args[0])
                else:
                    inner.append(x)
            capt = f"<figcaption>{cap}</figcaption>" if cap else ""
            return f'<figure>{self.blocks_html(inner)}{capt}</figure>'
        if n in ("center", "minipage", "flushleft", "flushright", "small", "footnotesize", "multicols", "tcolorbox"):
            return self.blocks_html(nd.body)
        if n == "abstract":
            return f'<div class="abstract">{self.blocks_html(nd.body)}</div>'
        if n in ("quote", "quotation"):
            return f"<blockquote>{self.blocks_html(nd.body)}</blockquote>"
        if n in ("equation", "align", "displaymath"):
            return f'<div class="dmath">{render_math(plain(nd.body))}</div>'
        warn(f"unknown environment {n}")
        return self.blocks_html(nd.body)


def plain(nodes):
    """Plain text of a node list (for labels, urls)."""
    if isinstance(nodes, str):
        return nodes
    out = []
    for nd in nodes:
        if nd.kind == "text":
            out.append(nd.text)
        elif nd.kind == "group":
            out.append(plain(nd.body))
        elif nd.kind == "verb":
            out.append(nd.text)
        elif nd.kind == "cmd" and nd.name in SIMPLE:
            out.append(SIMPLE[nd.name])
        elif nd.kind == "cmd" and nd.args:
            out.append(plain(nd.args[-1]))
        elif nd.kind == "math":
            out.append(nd.text)
    return "".join(out).strip()


# ============================================================ source assembly

def expand_inputs(text, base):
    text = strip_comments(text)

    def rep(m):
        p = base / m.group(1)
        if not p.suffix:
            p = p.with_suffix(".tex")
        if "#" in m.group(1):
            return ""          # the \diagram macro body, not a real file
        if p.exists():
            return "\n" + expand_inputs(p.read_text(encoding="utf-8"), base) + "\n"
        warn(f"input missing: {m.group(1)}")
        return ""
    return re.sub(r"\\input\{([^}]+)\}", rep, text)


def extract_verbatim(text):
    blocks = {}
    envs = "|".join(VERBATIM_ENVS)
    pat = re.compile(r"\\begin\{(" + envs + r")\}(?:\[[^\]]*\])?\n?(.*?)\\end\{\1\}", re.S)

    def rep(m):
        bid = str(len(blocks))
        blocks[bid] = (m.group(1), m.group(2))
        return f"\x00{bid}\x00"
    return pat.sub(rep, text), blocks


def strip_comments(text):
    return re.sub(r"(?<!\\)%[^\n]*", "", text)


def body_of(text):
    m = re.search(r"\\begin\{document\}(.*)\\end\{document\}", text, re.S)
    return m.group(1) if m else text


CSS = r"""
:root{--bg:#fbfbf9;--fg:#1d1d1b;--muted:#5d5d58;--line:#d8d8d2;--accent:@ACCENT@;--accent-bg:@ACCENT_BG@;
  --code-bg:#f2f2ee;--note-bg:#fff8dc;--note-line:#d99a2b;--kf-bg:@ACCENT_BG@;--kf-line:@KF_LINE@;--fig-bg:#fff;
  --hw:#ffecb3;--ker:#c5deff;--usr:#cdf0cd;}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#15161a;--fg:#e6e6e2;--muted:#a6a6a0;
  --line:#34363c;--accent:@ACCENT_D@;--accent-bg:@ACCENT_BG_D@;--code-bg:#1e2026;--note-bg:#2a2614;--note-line:#c9902b;
  --kf-bg:@ACCENT_BG_D@;--kf-line:@KF_LINE_D@;--fig-bg:#f4f4f0;}}
:root[data-theme="dark"]{--bg:#15161a;--fg:#e6e6e2;--muted:#a6a6a0;--line:#34363c;--accent:@ACCENT_D@;--accent-bg:@ACCENT_BG_D@;
  --code-bg:#1e2026;--note-bg:#2a2614;--note-line:#c9902b;--kf-bg:@ACCENT_BG_D@;--kf-line:@KF_LINE_D@;--fig-bg:#f4f4f0;}
html{scroll-behavior:smooth}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.55 -apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
.layout{display:grid;grid-template-columns:280px minmax(0,1fr);min-height:100vh}
nav.toc{position:sticky;top:0;height:100vh;overflow:auto;border-right:1px solid var(--line);padding:18px 14px;font-size:13.5px;background:var(--bg)}
nav.toc h2{font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);margin:0 0 8px}
nav.toc a{color:var(--fg);text-decoration:none;display:block;padding:2px 0}
nav.toc a:hover{color:var(--accent)}
nav.toc .l1{font-weight:700;margin-top:10px;color:var(--accent)}
nav.toc .l2{padding-left:10px}
main{padding:28px 48px 80px;max-width:980px}
header.title{border-bottom:2px solid var(--accent);margin-bottom:24px;padding-bottom:12px}
header.title h1{font-size:34px;margin:0 0 4px;line-height:1.15}
header.title .sub{font-size:20px;color:var(--muted)}
header.title .by{margin-top:8px;color:var(--muted)}
h1.part{font-size:30px;margin:60px 0 10px;padding-top:22px;border-top:3px solid var(--accent);color:var(--accent)}
h2{font-size:25px;margin:48px 0 6px;line-height:1.2}
h2.project{margin-top:70px;padding-top:26px;border-top:1px solid var(--line)}
h2.project .pnum{display:block;font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--accent);margin-bottom:4px}
h3{font-size:19px;margin:30px 0 6px}
h4{font-size:16px;margin:20px 0 4px}
p.meta{margin:0 0 16px;color:var(--muted);font-size:14px}
p.meta span{margin-right:18px}
p{margin:8px 0}
code{font-family:ui-monospace,Consolas,"Cascadia Mono",Menlo,monospace;font-size:.9em;background:var(--code-bg);padding:1px 4px;border-radius:3px}
pre{background:var(--code-bg);border:1px solid var(--line);border-radius:6px;padding:10px 12px;overflow-x:auto;font-size:13px;line-height:1.4;margin:10px 0}
pre code{background:none;padding:0;font-size:inherit}
pre.ascii{background:transparent;border:1px dashed var(--line);font-size:12.5px;line-height:1.25}
figure{margin:18px 0}
figure.diagram{text-align:center}
figure.diagram .svgwrap{background:var(--fig-bg);border-radius:6px;padding:10px;display:inline-block;max-width:100%;box-sizing:border-box}
figure.diagram svg{max-width:100%;height:auto;display:block}
/* dvisvgm positions every glyph run itself; let the browser add nothing on top */
figure.diagram svg text{font-kerning:none;font-variant-ligatures:none;font-feature-settings:"kern" 0;text-rendering:geometricPrecision;white-space:pre}
figcaption{font-size:14px;color:var(--muted);margin-top:6px;text-align:left}
figure.diagram figcaption{text-align:center}
.tablewrap{overflow-x:auto}
table{border-collapse:collapse;font-size:14px;margin:6px 0;min-width:50%}
th,td{border-bottom:1px solid var(--line);padding:5px 10px;text-align:left;vertical-align:top}
th{border-bottom:2px solid var(--fg);font-weight:700}
.keyfacts{background:var(--kf-bg);border:1px solid var(--kf-line);border-radius:8px;padding:10px 14px;margin:14px 0}
.kf-title{font-weight:700;color:var(--kf-line);margin-bottom:4px}
dl.kf{display:grid;grid-template-columns:max-content 1fr;gap:3px 16px;margin:0}
dl.kf dt{font-weight:700}
dl.kf dd{margin:0}
dl{margin:8px 0}dt{font-weight:700;margin-top:6px}dd{margin:0 0 4px 18px}
aside.note{background:var(--note-bg);border-left:4px solid var(--note-line);padding:8px 14px;margin:14px 0;border-radius:0 6px 6px 0}
aside.note .note-title{font-weight:700;margin-bottom:2px}
ol.steps>li{margin:8px 0}
ol.steps>li::marker{font-weight:700}
ul,ol{padding-left:24px}li{margin:3px 0}
.abstract{font-size:15px;border-left:3px solid var(--line);padding-left:14px;color:var(--muted)}
.missing{color:#b00;font-style:italic}
.math{font-family:"Times New Roman",Georgia,serif;font-style:italic}
.dmath{text-align:center;margin:10px 0}
.fn{color:var(--muted);font-size:.9em}
.runin{margin-right:4px}
a{color:var(--accent)}
@media (max-width:900px){.layout{grid-template-columns:1fr}nav.toc{position:static;height:auto;border-right:0;border-bottom:1px solid var(--line)}main{padding:18px 20px}}
@media print{nav.toc{display:none}.layout{display:block}main{max-width:none;padding:0}h2.project{break-before:page}pre{white-space:pre-wrap}}
"""


def css():
    """CSS with this volume's accent family substituted for the sentinels."""
    out = CSS
    for key in ("accent", "accent_bg", "kf_line"):
        out = out.replace("@" + key.upper() + "@", DOC[key]["light"])
        out = out.replace("@" + key.upper() + "_D@", DOC[key]["dark"])
    return out


def build_html(text, figures, out_path, title=None, subtitle=None,
               author=None, date=None, standalone_fragment=False):
    title = DOC["title"] if title is None else title
    subtitle = DOC["subtitle"] if subtitle is None else subtitle
    author = DOC["author"] if author is None else author
    date = DOC["date"] if date is None else date
    body = body_of(text) if not standalone_fragment else text
    body, blocks = extract_verbatim(body)
    body = strip_comments(body)
    nodes = Tokenizer(body).parse("__none__")
    r = Renderer(blocks, figures)
    content = r.blocks_html(nodes)
    toc = ['<nav class="toc"><h2>Contents</h2>']
    for level, hid, text_ in r.toc:
        toc.append(f'<a class="l{level}" href="#{html.escape(hid)}">{html.escape(text_)}</a>')
    toc.append("</nav>")
    head = (f'<header class="title"><h1>{html.escape(title)}</h1>'
            f'<div class="sub">{html.escape(subtitle)}</div>'
            f'<div class="by">{html.escape(author)} · {html.escape(date)}</div></header>')
    content = content.replace("<!--TITLE-->", head).replace("<!--TOC-->", "")
    if "<header" not in content:
        content = head + content
    page = (f'<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            f'<title>{html.escape(title)} {html.escape(subtitle)}</title>'
            f'<style>{css()}</style></head><body><div class="layout">'
            + "".join(toc) + f"<main>{content}</main></div></body></html>")
    Path(out_path).write_text(page, encoding="utf-8")


ID_ATTR = re.compile(r'\bid="([^"]+)"')
ID_REF = re.compile(r'(href|xlink:href|url\()(=?)"?#([^"\)]+)')


def inline_svg(name, svg_text):
    """Make one dvisvgm SVG safe to paste into a page that holds 80 others."""
    svg_text = re.sub(r"<\?xml[^>]*\?>\s*", "", svg_text)
    svg_text = re.sub(r"<!--.*?-->\s*", "", svg_text, flags=re.S)
    ids = set(ID_ATTR.findall(svg_text))
    pre = f"{name}-"
    if ids:
        svg_text = ID_ATTR.sub(lambda m: f'id="{pre}{m.group(1)}"', svg_text)

        def ref(m):
            tgt = m.group(3)
            return f'{m.group(1)}{m.group(2)}"#{pre}{tgt}' if tgt in ids else m.group(0)
        svg_text = ID_REF.sub(ref, svg_text)
    # font-family names are global too, so namespace them as well
    for fam in sorted(set(re.findall(r"font-family:\s*([A-Za-z0-9_-]+)", svg_text)) |
                      set(re.findall(r"font-family='([^']+)'", svg_text)) |
                      set(re.findall(r'font-family="([^"]+)"', svg_text)), key=len, reverse=True):
        svg_text = svg_text.replace(fam, pre + fam)
    # keep width and height: they carry the intrinsic size the CSS scales from
    return svg_text


def svg_data_uris(names, force=False):
    """name -> inlineable SVG markup (None when the figure failed to render)."""
    out = {}
    for n in names:
        path = build_figure(n, force=force)
        out[n] = inline_svg(n, path.read_text(encoding="utf-8")) if path else None
    return out


def check_ascii_in_code(text, label):
    _, blocks = extract_verbatim(text)
    for bid, (env, code) in blocks.items():
        bad = sorted({c for c in code if ord(c) > 126})
        if bad:
            warn(f"{label}: non-ASCII in {env} block: {''.join(bad)!r} (pdflatex listings cannot print these)")
        limit = DOC["ascii_cols"] if env == "asciiart" else DOC["code_cols"]
        for ln in code.splitlines():
            if len(ln) > limit:
                warn(f"{label}: {env} line is {len(ln)} chars (max {limit}); it will "
                     f"overflow or wrap: {ln[:40]!r}...")


def preamble_of(main_text):
    return main_text.split("\\begin{document}")[0]


def chapter_slug(text, number):
    """07-the-actuator-you-do-not-have, from the \\project title.

    The unit word is not in the slug. A file named after one unit of this
    volume prepends DOC["unit"], so the same function serves a volume of
    chapters and a volume of projects without either borrowing the other's
    vocabulary.
    """
    m = re.search(r"\\project\{(\d+)\}\{([^}]*)\}", text)
    title = m.group(2) if m else ""
    title = title.split(":")[0].split(",")[0].lower()
    # Drop apostrophes rather than letting them become separators, so that
    # "the joint's inner ear" does not slugify to "the-joint-s-inner-ear".
    title = title.replace("'", "").replace("’", "")
    title = re.sub(r"[^a-z0-9]+", "-", title).strip("-")
    words = [w for w in title.split("-") if w][:8]
    stop = {"a", "an", "and", "as", "at", "by", "for", "in", "of", "on",
            "or", "the", "to", "with", "you"}
    while words and words[-1] in stop:
        words.pop()
    return f"{number:02d}" + ("-" + "-".join(words) if words else "")


def build_chapter(which):
    """Build one chapter as a shareable PDF and one self-contained HTML file.

    The book is handed out a chapter at a time, so a chapter is a deliverable
    in its own right rather than an excerpt: it carries its own title page and
    its own figures, and it is named after itself rather than after the book.
    """
    if re.fullmatch(r"\d{1,2}", str(which)):
        # The section file prefix comes from the DOC block, so this works
        # unchanged in every volume: "sec:j" here, "sec:c" in the firmware
        # volume, "sec:p" in the Linux one.
        pfx = DOC["label_pfx"].split(":")[-1]
        path = SECTIONS / f"{pfx}{int(which):02d}.tex"
    else:
        path = Path(which)
        if not path.is_absolute():
            path = (ROOT / path).resolve()
    if not path.exists():
        print(f"no such chapter: {path.name}")
        return False
    text = path.read_text(encoding="utf-8")
    m = re.search(r"\\project\{(\d+)\}\{([^}]*)\}", text)
    if not m:
        print(f"{path.name} has no \\project line, so it is not a chapter")
        return False
    number, title = int(m.group(1)), m.group(2)
    stem = f'{DOC["unit"].lower()}-{chapter_slug(text, number)}'
    check_ascii_in_code(text, path.name)
    BUILD.mkdir(exist_ok=True)

    # A chapter handed out on its own still has to know which chapter it is:
    # set the counter so the heading numbers itself correctly, and put the
    # chapter rather than the book in the PDF metadata.
    meta = re.sub(r"[^ A-Za-z0-9,.:()'-]", "", f"{DOC['unit']} {number}. {title}")
    stub = (preamble_of(MAIN.read_text(encoding="utf-8"))
            + "\\hypersetup{pdftitle={" + meta + "}}\n"
            + "\\begin{document}\n"
            + f"\\setcounter{{section}}{{{number - 1}}}\n"
            + "\\input{" + path.relative_to(ROOT).as_posix() + "}\n\\end{document}\n")
    (BUILD / f"{stem}.tex").write_text(stub, encoding="utf-8")
    for _ in range(2):
        r = run(["pdflatex", "-interaction=nonstopmode", "-file-line-error",
                 "-output-directory=build", f"build/{stem}.tex"], timeout=600)
    log = BUILD / f"{stem}.log"
    logtext = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    errs = tex_errors(logtext) if logtext else ["no log"]
    if r.returncode != 0 or errs:
        print(f"== pdflatex problems in {path.name}:")
        for e in errs[:8]:
            print(e)
        return False
    shutil.copy2(BUILD / f"{stem}.pdf", ROOT / f"{stem}.pdf")
    pages = re.search(r"\((\d+) pages?,", logtext)
    print(f'== {DOC["unit"].lower()} {number}: {title}')
    print(f"   PDF  -> {stem}.pdf  ({pages.group(1) if pages else '?'} pages)")

    names = all_figure_names(text)
    uris = svg_data_uris(names, force=True)
    missing = [n for n in names if not uris.get(n)]
    if missing:
        print("   figures FAILED:", ", ".join(missing))
        return False
    build_html(text, uris, ROOT / f"{stem}.html",
               title=f"{DOC['unit']} {number}. {title}",
               subtitle=DOC["title"], standalone_fragment=True)
    print(f"   HTML -> {stem}.html  ({len(names)} figures inlined)")
    return True


def check_section(path):
    path = Path(path)
    if not path.is_absolute():
        path = (ROOT / path).resolve()
    text = path.read_text(encoding="utf-8")
    label = path.name
    check_ascii_in_code(text, label)
    BUILD.mkdir(exist_ok=True)
    main_text = MAIN.read_text(encoding="utf-8")
    stub = preamble_of(main_text) + "\\begin{document}\n\\tableofcontents\n\\input{" + path.relative_to(ROOT).as_posix() + "}\n\\end{document}\n"
    stubname = f"check_{path.stem}.tex"
    (BUILD / stubname).write_text(stub, encoding="utf-8")
    ok = True
    for _ in range(2):
        r = run(["pdflatex", "-interaction=nonstopmode", "-file-line-error", "-output-directory=build",
                 f"build/{stubname}"], timeout=600)
    log = BUILD / f"check_{path.stem}.log"
    errs = tex_errors(log.read_text(encoding="utf-8", errors="replace")) if log.exists() else ["no log"]
    if r.returncode != 0 or errs:
        ok = False
        print(f"== pdflatex problems in {label}:")
        for e in errs[:8]:
            print(e)
    else:
        pages = re.search(r"\((\d+) pages?,", log.read_text(encoding="utf-8", errors="replace"))
        print(f"== pdflatex OK for {label}: {pages.group(1) if pages else '?'} pages -> build/check_{path.stem}.pdf")
    overfull = len(re.findall(r"Overfull \\hbox \((\d+\.\d+)pt", log.read_text(encoding="utf-8", errors="replace")))
    if overfull:
        print(f"   {overfull} overfull hbox warnings (long lines in tables/paragraphs); acceptable if small")
    names = all_figure_names(text)
    uris = svg_data_uris(names, force=True)
    for n in names:
        print(f"   figure {n}: {'ok' if uris.get(n) else 'FAILED'}")
    out = BUILD / f"check_{path.stem}.html"
    build_html(text, uris, out, standalone_fragment=True)
    print(f"   HTML fragment -> {out.relative_to(ROOT).as_posix()}")
    if WARNINGS:
        ok = False
        print("== warnings:")
        for w in WARNINGS:
            print("   " + w)
    print("== RESULT:", "CLEAN" if ok else "NEEDS FIXES")
    return ok


def toolchain_drift(other):
    """Diff this build.py against a sibling volume's, ignoring the DOC block.

    Each product folder is self-contained by design, so the two copies are never
    shared. This is how they are kept identical anyway: a converter fix made in
    one volume can be found and applied to the other.
    """
    import difflib

    sibling = re.compile(r"\.\./Embedded\w+/build\.py"
                         r"|sections/[a-z]\d\d\.tex"
                         r"|the (?:joint-node|firmware|Top 20) volume")

    def strip_doc(path):
        lines = Path(path).read_text(encoding="utf-8").splitlines()
        # A line whose only difference is the name of a sibling volume is not
        # drift: it is this copy saying which other copy to compare against.
        lines = [sibling.sub("<sibling>", l) for l in lines]
        try:
            i = next(k for k, l in enumerate(lines) if l.startswith("DOC = {"))
            j = next(k for k in range(i, len(lines)) if lines[k].rstrip() == "}")
        except StopIteration:
            return lines
        return lines[:i] + lines[j + 1:]

    d = list(difflib.unified_diff(strip_doc(__file__), strip_doc(other),
                                  "this", str(other), lineterm=""))
    print("\n".join(d) if d else "toolchain identical outside the DOC block")
    return not d


def main(argv):
    if "--drift" in argv:
        i = argv.index("--drift")
        sys.exit(0 if toolchain_drift(argv[i + 1]) else 1)
    if "--check" in argv:
        i = argv.index("--check")
        sys.exit(0 if check_section(argv[i + 1]) else 1)
    if "--chapter" in argv:
        i = argv.index("--chapter")
        if i + 1 >= len(argv):
            sys.exit("--chapter needs a number, for example: --chapter 7")
        sys.exit(0 if build_chapter(argv[i + 1]) else 1)
    if "--chapters" in argv:
        ok = all(build_chapter(n) for n in range(1, 21))
        sys.exit(0 if ok else 1)
    do_pdf = "--pdf" in argv or not any(a.startswith("--") for a in argv)
    do_html = "--html" in argv or not any(a.startswith("--") for a in argv)
    do_fig = "--figures" in argv or do_html
    main_text = MAIN.read_text(encoding="utf-8")
    full = expand_inputs(main_text, ROOT)
    if do_fig:
        names = all_figure_names(full)
        uris = svg_data_uris(names, force="--force" in argv)
        print(f"figures: {sum(1 for v in uris.values() if v)} of {len(uris)} rendered")
    if do_pdf:
        build_pdf()
    if do_html:
        check_ascii_in_code(full, "document")
        build_html(full, uris, ROOT / HTML_NAME)
        print(f"HTML written: {HTML_NAME}")
    if WARNINGS:
        print("warnings:")
        for w in WARNINGS:
            print("  " + w)


if __name__ == "__main__":
    main(sys.argv[1:])
