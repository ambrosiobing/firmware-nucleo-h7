#!/usr/bin/env python3
"""Generate the Markdown edition of this volume, one file per chapter.

    python mdbuild.py                 front matter, twenty chapters, appendix, index
    python mdbuild.py --chapter 7     one chapter, for looking at before the rest
    python mdbuild.py --out ../md     write somewhere other than the volume itself

Output is chapters/NN-title.md, figures/NAME.svg beside the figure sources, and
CONTENTS.md, which is the chapter table the README links from.

Why this exists. The PDF and the single-file HTML are the reading editions, and
both are built locally and stay local. What gets published is the source, and a
reader who opens the published repository in a browser sees whatever the host
renders. A host renders Markdown and does not render LaTeX, so a repository of
.tex files presents as markup rather than as a book. This converter produces the
same twenty chapters as Markdown with the figures beside them as SVG, so the
published repository reads as a book in the browser with nothing installed.

It reuses build.py rather than reimplementing it: the same tokenizer, the same
LaTeX subset, the same figure list. Only the block and inline output layer is
replaced, so a macro added to main.tex is understood here the moment build.py
understands it, and an unknown command is reported by the same warning path.

What deliberately degrades, and why that is acceptable:

  - A table cell cannot hold a code block in Markdown's pipe tables. A table
    that needs one falls back to an HTML table, which the host also renders.
  - Column spans are dropped. The text stays; the span does not.
  - Figure and table references become their printed name rather than a link,
    because anchor targets inside a rendered Markdown file are rewritten by the
    host and a link that silently goes nowhere is worse than plain text.
  - Chapter references do become links, to the chapter's own file.
"""
import argparse
import re
import sys
from pathlib import Path

import build
from build import DOC, ROOT, SECTIONS, Tokenizer, plain, warn

MD_SPECIAL = re.compile(r"([\\`*_\[\]<>|])")
FIG_ORDER = ("arch", "wiring", "uml", "data", "timing")


def md_escape(t):
    return MD_SPECIAL.sub(r"\\\1", t)


def one_line(t):
    """Collapse rendered Markdown to something that survives inside a table cell."""
    t = t.replace("\n", " ")
    t = t.replace("|", "\\|")
    return re.sub(r"\s+", " ", t).strip()


def indent_after_first(text, width):
    """Indent every line but the first, so a block stays inside its list item."""
    lines = text.split("\n")
    pad = " " * width
    return "\n".join([lines[0]] + [(pad + l if l.strip() else "") for l in lines[1:]])


def quote(text, marker="> "):
    return "\n".join((marker + l).rstrip() if l.strip() else ">"
                     for l in text.split("\n"))


class MarkdownRenderer(build.Renderer):
    """build.Renderer with the output layer replaced. The parse layer is shared."""

    LEVELS_CHAPTER = {"section": 2, "subsection": 2, "subsubsection": 3}
    LEVELS_PLAIN = {"section": 2, "subsection": 3, "subsubsection": 4}

    def __init__(self, blocks, number=None, chapter_files=None, levels=None):
        super().__init__(blocks, {})
        self.levels = levels or self.LEVELS_CHAPTER
        self.number = number            # chapter number, for "Figure 7.3"
        self.chapter_files = chapter_files or {}
        self.fig_names = {}             # figure name -> printed number
        self.tab_labels = {}            # table label -> printed number

    # -------------------------------------------------- numbering, done first
    def prescan(self, nodes):
        """Walk the tree in document order and assign figure and table numbers."""
        fig = tab = 0
        stack = list(nodes)
        order = []
        self._walk(nodes, order)
        for kind, key in order:
            if kind == "fig":
                fig += 1
                label = self.label_for("Figure", fig)
                # Under both keys: block_cmd knows the figure by its bare name,
                # a \ref knows it by its LaTeX label.
                self.fig_names[key] = label
                self.fig_names[f"fig:{key}"] = label
            else:
                tab += 1
                if key:
                    self.tab_labels[key] = self.label_for("Table", tab)

    def _walk(self, nodes, order):
        for nd in nodes:
            if nd.kind == "cmd" and nd.name == "diagram":
                order.append(("fig", plain(nd.args[0])))
            elif nd.kind == "env" and nd.name == "table":
                lab = None
                for x in nd.body:
                    if x.kind == "cmd" and x.name == "label":
                        lab = plain(x.args[0])
                order.append(("tab", lab))
                self._walk(nd.body, order)
            elif nd.kind in ("env", "group"):
                self._walk(nd.body, order)
            elif nd.kind == "cmd":
                for a in nd.args:
                    if isinstance(a, list):
                        self._walk(a, order)

    def label_for(self, word, n):
        return f"{word} {self.number}.{n}" if self.number else f"{word} {n}"

    # -------------------------------------------------- inline
    def text_html(self, t):
        t = t.replace("``", "\u201c").replace("''", "\u201d")
        t = t.replace("---", "-").replace("--", "-")
        t = re.sub(r"(?<![\w])`", "\u2018", t)
        t = re.sub(r"'(?=[\s.,;:!?)]|$)", "\u2019", t)
        return md_escape(t)

    def code_span(self, text):
        fence = "`"
        while fence in text:
            fence += "`"
        pad = " " if text.startswith("`") or text.endswith("`") else ""
        return f"{fence}{pad}{text}{pad}{fence}"

    def cmd(self, nd):
        n, a = nd.name, nd.args
        if n in build.SIMPLE:
            v = build.SIMPLE[n]
            return {"<br>": "  \n", "\\": "\\\\"}.get(v, v)
        if n == "\\\\":
            return "  \n"
        if n == "textbf":
            return f"**{self.inline(a[0])}**"
        if n in ("emph", "textit"):
            return f"*{self.inline(a[0])}*"
        if n in ("texttt", "path", "nolinkurl"):
            return self.code_span(plain(a[0]))
        if n in ("textsf", "textsc", "mbox", "text", "pubdate", "hyperref",
                 "fbox", "makebox", "texorpdfstring", "hl", "st"):
            return self.inline(a[0]) if a else ""
        if n == "underline":
            return self.inline(a[0])
        if n == "textsuperscript":
            return f"<sup>{self.inline(a[0])}</sup>"
        if n == "textsubscript":
            return f"<sub>{self.inline(a[0])}</sub>"
        if n == "url":
            return f"<{plain(a[0])}>"
        if n == "href":
            return f"[{self.inline(a[1])}]({plain(a[0])})"
        if n in ("ref", "autoref", "nameref"):
            return self.reference(plain(a[0]))
        if n in ("pageref", "label", "caption"):
            return ""
        if n == "footnote":
            return f" ({self.inline(a[0])})"
        if n == "textcolor":
            return self.inline(a[1])
        if n == "parbox":
            return self.inline(a[1])
        if n == "resizebox":
            return self.inline(a[2])
        if n == "multicolumn":
            return self.inline(a[2])
        if n == "includegraphics":
            return ""
        if n in ("section", "subsection", "subsubsection", "paragraph", "part",
                 "project", "diagram", "tableofcontents", "item"):
            return self.block_cmd(nd)
        if n in build.IGNORE:
            return ""
        warn(f"unknown command \\{n}")
        return ""

    def reference(self, lab):
        if lab in self.fig_names:
            return f"**{self.fig_names[lab]}**"
        if lab in self.tab_labels:
            return f"**{self.tab_labels[lab]}**"
        m = re.match(re.escape(DOC["label_pfx"]) + r"(\d+)", lab)
        if m:
            num = int(m.group(1))
            text = f'{DOC["unit"]} {num}'
            target = self.chapter_files.get(num)
            if target and num != self.number:
                return f"[{text}]({target})"
            return f"**{text}**"
        if lab.startswith("fig:"):
            return "the figure above"
        if lab.startswith("tab:"):
            return "the table above"
        return md_escape(lab)

    # -------------------------------------------------- blocks
    def blocks_html(self, nodes):
        out, para = [], []

        def flush():
            s = "".join(para).strip()
            if s:
                s = re.sub(r"(?<!\n)[ \t]{2,}", " ", s)
                s = re.sub(r"[ \t]+\n", "  \n", s)
                # "Figure~\ref{fig:x}" would otherwise read "Figure Figure 3".
                s = re.sub(r"\b(Figure|Table)\s+(\*\*\1 )", r"\2", s)
                out.append(s)
            para.clear()

        for nd in nodes:
            if nd.kind == "par":
                flush()
            elif nd.kind == "block":
                flush()
                out.append(self.block_code(nd.text))
            elif nd.kind == "env" and nd.name != "group":
                flush()
                out.append(self.env(nd))
            elif nd.kind == "cmd" and nd.name == "paragraph":
                flush()
                para.append(f"**{self.inline(nd.args[0])}** ")
            elif nd.kind == "cmd" and nd.name in (
                    "section", "subsection", "subsubsection", "part", "project",
                    "diagram", "tableofcontents", "item", "maketitle"):
                flush()
                out.append(self.block_cmd(nd))
            elif nd.kind == "dmath":
                flush()
                out.append(f"`{nd.text.strip()}`")
            else:
                para.append(self.inline_node(nd))
        flush()
        return "\n\n".join(x for x in out if x and x.strip())

    def block_cmd(self, nd):
        n, a = nd.name, nd.args
        if n == "part":
            return f"# {self.inline(a[0])}"
        if n in self.levels:
            return "#" * self.levels[n] + " " + self.inline(a[0])
        if n == "paragraph":
            return f"**{self.inline(a[0])}**"
        if n == "project":
            num = int(plain(a[0]))
            title = self.inline(a[1])
            return (f'# {DOC["unit"]} {num}. {title}\n\n'
                    f'> **{DOC["meta1"]}:** {one_line(self.inline(a[2]))}  \n'
                    f'> **{DOC["meta2"]}:** {one_line(self.inline(a[3]))}')
        if n == "diagram":
            name = plain(a[0])
            cap = one_line(self.inline(a[1]))
            num = self.fig_names.get(name, "Figure")
            # The caption is printed under the figure, so the alt text is the
            # opening sentence rather than the whole of it. Repeating a long
            # caption verbatim reads it out twice to anyone using a screen
            # reader, which is worse than a short description.
            alt = re.sub(r"[\[\]\\*`_]", "", cap).split(". ")[0]
            return (f"![{num}. {alt}.](../figures/{name}.svg)\n\n"
                    f"*{num}. {cap}*")
        if n in ("tableofcontents", "item", "maketitle"):
            return ""
        return ""

    def block_code(self, bid):
        env, code = self.blocks[bid]
        code = code.strip("\n")
        lang = {"ccode": "c", "cppcode": "cpp", "rustcode": "rust",
                "shellcode": "bash", "pycode": "python", "makecode": "make",
                "dtscode": "dts", "yamlcode": "yaml", "asmcode": "armasm",
                "ldcode": "ld", "asciiart": "text"}.get(env, "text")
        fence = "```"
        while fence in code:
            fence += "`"
        return f"{fence}{lang}\n{code}\n{fence}"

    # -------------------------------------------------- lists
    def render_list(self, tag, nodes, cls=""):
        items = self.split_items(nodes)
        out = []
        for i, (label, body) in enumerate(items, 1):
            inner = self.blocks_html(body).strip()
            if label is not None:
                lab = self.inline(Tokenizer(label).parse("__none__")).strip()
                inner = f"**{lab}** {inner}" if inner else f"**{lab}**"
            marker = f"{i}. " if tag == "ol" else "- "
            out.append(marker + indent_after_first(inner, len(marker)))
        return "\n".join(out) if all("\n\n" not in x for x in out) else "\n\n".join(out)

    def render_steps(self, nodes):
        """Steps carry code blocks and commands. A heading per step beats a list."""
        out = []
        for i, (label, body) in enumerate(self.split_items(nodes), 1):
            inner = self.blocks_html(body).strip()
            head = f"**Step {i}.**"
            if inner.startswith("```") or inner.startswith("!["):
                out.append(head)
                out.append(inner)
            else:
                first, _, rest = inner.partition("\n\n")
                out.append(f"{head} {first}")
                if rest:
                    out.append(rest)
        return "\n\n".join(out)

    def render_dl(self, nodes, cls=""):
        out = []
        for label, body in self.split_items(nodes):
            lab = self.inline(Tokenizer(label or "").parse("__none__")).strip()
            inner = self.blocks_html(body).strip()
            out.append("- " + indent_after_first(f"**{lab}:** {inner}", 2))
        return "\n".join(out)

    # -------------------------------------------------- tables
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

        header_rows = 0
        for i, r in enumerate(rows):
            if any(y.kind == "cmd" and y.name == "midrule" for y in r):
                header_rows = i
                break

        grid = []
        for r in rows:
            cells, cell = [], []
            for y in r:
                if y.kind == "amp":
                    cells.append(cell)
                    cell = []
                elif y.kind == "cmd" and y.name in ("toprule", "midrule", "bottomrule",
                                                    "hline", "cmidrule"):
                    continue
                else:
                    cell.append(y)
            cells.append(cell)
            if all(not plain(c).strip() for c in cells):
                continue
            grid.append(cells)
        if not grid:
            return ""

        if any(z.kind == "block" for cells in grid for c in cells for z in c):
            return super().render_table(nd)      # code in a cell: HTML table

        width = max(len(c) for c in grid)
        text = [[one_line(self.inline(c)) for c in cells] + [""] * (width - len(cells))
                for cells in grid]

        if header_rows == 0:
            head = [""] * width
            body = text
        elif header_rows == 1:
            head, body = text[0], text[1:]
        else:
            head = ["<br>".join(x for x in col if x).strip()
                    for col in zip(*text[:header_rows])]
            body = text[header_rows:]

        lines = ["| " + " | ".join(head) + " |",
                 "|" + "|".join([" --- "] * width) + "|"]
        lines += ["| " + " | ".join(r) + " |" for r in body]
        return "\n".join(lines)

    # -------------------------------------------------- environments
    def env(self, nd):
        n = nd.name
        if n == "itemize":
            return self.render_list("ul", nd.body)
        if n == "enumerate":
            return self.render_list("ol", nd.body)
        if n == "steps":
            return self.render_steps(nd.body)
        if n == "description":
            return self.render_dl(nd.body)
        if n == "keyfacts":
            return quote(f'**{DOC["keyfacts"]}**\n\n' + self.render_dl(nd.body))
        if n == "note":
            title = one_line(self.inline(nd.args[0])) if nd.args else "Note"
            body = self.blocks_html(nd.body)
            return "> [!NOTE]\n" + quote(f"**{title}**\n\n{body}")
        if n in ("tabular", "tabularx", "longtable"):
            return self.render_table(nd)
        if n == "table":
            cap, inner = None, []
            for x in nd.body:
                if x.kind == "cmd" and x.name == "caption":
                    cap = one_line(self.inline(x.args[0]))
                elif x.kind == "cmd" and x.name == "label":
                    pass
                else:
                    inner.append(x)
            self.tab_no += 1
            num = self.label_for("Table", self.tab_no)
            body = self.blocks_html(inner)
            return f"{body}\n\n*{num}.{' ' + cap if cap else ''}*"
        if n == "figure":
            cap, inner = None, []
            for x in nd.body:
                if x.kind == "cmd" and x.name == "caption":
                    cap = one_line(self.inline(x.args[0]))
                else:
                    inner.append(x)
            body = self.blocks_html(inner)
            return f"{body}\n\n*{cap}*" if cap else body
        if n in ("center", "minipage", "flushleft", "flushright", "small",
                 "footnotesize", "multicols", "tcolorbox", "group"):
            return self.blocks_html(nd.body)
        if n == "abstract":
            return quote(self.blocks_html(nd.body))
        if n in ("quote", "quotation"):
            return quote(self.blocks_html(nd.body))
        if n in ("equation", "align", "displaymath"):
            return f"`{plain(nd.body).strip()}`"
        warn(f"unknown environment {n}")
        return self.blocks_html(nd.body)


# ---------------------------------------------------------------- figures
SVG_TAG = re.compile(r"<svg\b[^>]*viewBox='([^']+)'[^>]*>", re.I)


def copy_svg(src, dst):
    """Copy a figure, giving it a white ground.

    dvisvgm draws black on nothing. A host that renders Markdown in a dark theme
    puts that on a dark page, where black line art is not there at all. An opaque
    ground behind the drawing is the one change that makes every figure legible
    in both themes.
    """
    text = src.read_text(encoding="utf-8")
    m = SVG_TAG.search(text)
    if m:
        try:
            x, y, w, h = (float(v) for v in m.group(1).replace(",", " ").split())
            rect = (f"<rect x='{x - 2:.3f}' y='{y - 2:.3f}' "
                    f"width='{w + 4:.3f}' height='{h + 4:.3f}' fill='#ffffff'/>")
            text = text[:m.end()] + "\n" + rect + text[m.end():]
        except ValueError:
            pass
    dst.write_text(text, encoding="utf-8")


# ---------------------------------------------------------------- driver
def chapter_sources():
    pfx = DOC["label_pfx"].split(":")[-1]
    return sorted(SECTIONS.glob(f"{pfx}[0-9][0-9].tex"))


def chapter_index():
    """number -> (filename, title, meta2) for every chapter, for links and the index."""
    out = {}
    for p in chapter_sources():
        text = p.read_text(encoding="utf-8")
        m = re.search(r"\\project\{(\d+)\}\{([^{}]*)\}\{([^{}]*)\}\{([^{}]*)\}", text)
        if not m:
            continue
        num = int(m.group(1))
        slug = build.chapter_slug(text, num)
        out[num] = (f"{slug}.md", m.group(2), m.group(4))
    return out


def render_file(path, number, chapter_files, levels=None):
    text = path.read_text(encoding="utf-8")
    body, blocks = build.extract_verbatim(text)
    body = build.strip_comments(body)
    nodes = Tokenizer(body).parse("__none__")
    r = MarkdownRenderer(blocks, number, chapter_files, levels)
    r.prescan(nodes)
    return r.blocks_html(nodes), build.all_figure_names(text)


FRONT_NAME = "00-about-this-volume.md"
APPENDIX_NAME = "21-appendix.md"


def nav(prev, nxt):
    links = ["[Contents](../README.md)"]
    if prev:
        links.insert(0, f"[Previous]({prev})")
    if nxt:
        links.append(f"[Next]({nxt})")
    return "---\n\n" + " &nbsp;&nbsp;|&nbsp;&nbsp; ".join(links) + "\n"


def write_figures(names, figures, written, missing):
    for name in names:
        svg = build.FIGBUILD / f"{name}.svg"
        if svg.exists():
            copy_svg(svg, figures / f"{name}.svg")
            written.add(name)
        elif name not in missing:
            missing.append(name)


def main(argv):
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--chapter", type=int, help="convert one chapter only")
    ap.add_argument("--out", default=".", help="output directory")
    args = ap.parse_args(argv)

    out = (ROOT / args.out).resolve()
    chapters, figures = out / "chapters", out / "figures"
    chapters.mkdir(parents=True, exist_ok=True)
    figures.mkdir(parents=True, exist_ok=True)

    index = chapter_index()
    links = {n: v[0] for n, v in index.items()}
    order = [FRONT_NAME] + [links[n] for n in sorted(index)] + [APPENDIX_NAME]
    around = {name: (order[i - 1] if i else None,
                     order[i + 1] if i + 1 < len(order) else None)
              for i, name in enumerate(order)}

    written, missing = set(), []
    pfx = DOC["label_pfx"].split(":")[-1]

    def emit(name, md, names):
        prev, nxt = around[name]
        (chapters / name).write_text(md.rstrip() + "\n\n" + nav(prev, nxt),
                                     encoding="utf-8")
        write_figures(names, figures, written, missing)
        print(f"  chapters/{name}  ({len(md):,} bytes, {len(names)} figures)")

    if not args.chapter:
        md, names = render_file(SECTIONS / "front.tex", None, links,
                                MarkdownRenderer.LEVELS_PLAIN)
        emit(FRONT_NAME,
             f'# {DOC["title"]}\n\n*{DOC["subtitle"]}*\n\n'
             f'{DOC["author"]}. {DOC["date"]}.\n\n' + md, names)

    for num in ([args.chapter] if args.chapter else sorted(index)):
        md, names = render_file(SECTIONS / f"{pfx}{num:02d}.tex", num, links)
        emit(links[num], md, names)

    if not args.chapter:
        md, names = render_file(SECTIONS / "appendix.tex", None, links,
                                MarkdownRenderer.LEVELS_PLAIN)
        emit(APPENDIX_NAME, "# Appendix\n\n" + md, names)

        table = ["| # | Chapter | Theme |", "|---|---|---|"]
        for n in sorted(index):
            fn, title, theme = index[n]
            table.append(f"| {n} | [{title}](chapters/{fn}) | {theme} |")
        (out / "CONTENTS.md").write_text("\n".join(table) + "\n", encoding="utf-8")
        print("  CONTENTS.md  (chapter table for the README)")

    if build.WARNINGS:
        print("warnings:")
        for w in sorted(set(build.WARNINGS)):
            print("  " + w)
    if missing:
        print("figures not rendered (run python build.py --figures): "
              + ", ".join(sorted(missing)))
    print(f"{len(written)} figures, into {out}")


if __name__ == "__main__":
    main(sys.argv[1:])
