#!/usr/bin/env python3
"""mksite.py [--check]
Build docs/manual.html, the User's Guide on the website, from MANUAL.md.

MANUAL.md is the source: the page is never edited by hand.  The guide is set
as a printed manual - numbered chapters, a contents column, keys drawn as
keys, figures - and that is all this adds; every word comes from the
Markdown.  Only the Markdown the manual actually uses is understood:
headings at two levels, paragraphs, bullet lists with wrapped lines, tables, and bold,
code, emphasis and links inline.

  tools/mksite.py          write docs/manual.html
  tools/mksite.py --check  fail if docs/manual.html is not what this makes
"""
import html
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC = os.path.join(ROOT, "MANUAL.md")
OUT = os.path.join(ROOT, "docs", "manual.html")
BLOB = "https://github.com/pedrocatalao/dos-ex-machina/blob/main/"

# A figure for a chapter, by the chapter's anchor: image, smaller image, alt,
# caption.  The pictures are the ones the front page uses.
FIGURES = {
    "the-machine": ("screenshots/prompt.jpg", "screenshots/prompt-960.jpg",
                    "The system configuration screen with the DOS prompt under it",
                    "The second BIOS screen, drawn by DOSBox itself, and DOS coming up under it."),
    "the-controls-on-the-case": ("img/detail-panel.jpg", None,
                                 "The maker's badge, the power button with its lamp, and the turbo display reading 66",
                                 "The power button and its lamp, and the turbo display with MODE, minus and plus."),
    "setup": ("screenshots/setup.jpg", "screenshots/setup-960.jpg",
              "SETUP on its MACHINE section",
              "SETUP, on MACHINE: the processor, the memory and the processor core."),
    "catalog": ("screenshots/catalog.jpg", "screenshots/catalog-960.jpg",
                "CATALOG with a list of freeware titles",
                "CATALOG. The dot beside a title says it is installed."),
}

# What is drawn as a key when the manual sets it in bold.
KEYS = {"Ctrl", "Shift", "Alt", "Command", "SPACE", "TAB", "ESC", "Esc", "Enter", "Left", "Right",
        "MODE", "−", "+"} | {"F%d" % n for n in range(1, 13)}


def slug(text):
    """GitHub's anchor for a heading, so links written for one work on both."""
    s = re.sub(r"[^\w\s-]", "", text.strip().lower())
    return re.sub(r"\s+", "-", s)


def keys_or_strong(inner):
    """**Ctrl+F10** is two keys; **The mouse** is emphasis."""
    raw = html.unescape(inner)
    if raw in KEYS:
        return "<kbd>%s</kbd>" % inner
    parts = raw.split("+")
    if len(parts) > 1 and all(p in KEYS for p in parts):
        return '<span class="plus">+</span>'.join("<kbd>%s</kbd>" % html.escape(p) for p in parts)
    return "<strong>%s</strong>" % inner


def link(m):
    text, href = m.group(1), html.unescape(m.group(2))
    if href.startswith("#") or href.startswith("http"):
        target = href
    else:  # a file in the repository: point at it on GitHub
        target = BLOB + href
    return '<a href="%s">%s</a>' % (html.escape(target, quote=True), text)


def inline(text):
    text = html.escape(text, quote=False)
    held = []

    def hold(m):  # code is kept out of the way of everything else
        held.append("<code>%s</code>" % m.group(1))
        return "\x00%d\x00" % (len(held) - 1)

    text = re.sub(r"`([^`]+)`", hold, text)
    text = re.sub(r"\*\*([^*]+)\*\*", lambda m: keys_or_strong(m.group(1)), text)
    text = re.sub(r"(?<![*\w])\*([^*\n]+)\*(?![*\w])", r"<em>\1</em>", text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, text)
    text = text.replace(" - ", " – ")
    return re.sub(r"\x00(\d+)\x00", lambda m: held[int(m.group(1))], text)


def blocks(lines):
    """Paragraphs, lists and tables, in order, from one chapter's lines."""
    out, i = [], 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
        elif line.startswith("### "):
            # A heading within a chapter.  The chapters themselves are the
            # numbered parts and come from `##`; this is a shelf inside one,
            # and takes no number of its own.
            out.append("<h3>%s</h3>" % inline(line[4:].strip()))
            i += 1
        elif line.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            head, body = rows[0], [r for r in rows[2:]]
            t = ['<div class="tablewrap"><table>']
            if any(head):
                t.append("<thead><tr>%s</tr></thead>" % "".join("<th>%s</th>" % inline(c) for c in head))
            t.append("<tbody>")
            for r in body:
                t.append("<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c) for c in r))
            t.append("</tbody></table></div>")
            out.append("".join(t))
        elif line.startswith("- "):
            items = []
            while i < len(lines) and (lines[i].startswith("- ") or (lines[i].startswith("  ") and items)):
                if lines[i].startswith("- "):
                    items.append(lines[i][2:].strip())
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            out.append("<ul>%s</ul>" % "".join("<li>%s</li>" % inline(x) for x in items))
        else:
            para = []
            while i < len(lines) and lines[i].strip() and not lines[i].startswith(("|", "- ", "### ")):
                para.append(lines[i].strip())
                i += 1
            out.append("<p>%s</p>" % inline(" ".join(para)))
    return out


def parse(md):
    """The title, the introduction, and the chapters as (heading, lines)."""
    title, intro, chapters = "", [], []
    for line in md.splitlines():
        if line.startswith("# ") and not title:
            title = line[2:].strip()
        elif line.startswith("## "):
            chapters.append((line[3:].strip(), []))
        elif chapters:
            chapters[-1][1].append(line)
        else:
            intro.append(line)
    # the Markdown's own contents list is for GitHub; the page has a column
    intro = [l for l in intro if not re.match(r"^- \[[^\]]+\]\(#[^)]+\)\s*$", l)]
    return title, intro, chapters


HEAD = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>User's Guide — DOS ex Machina</title>
<meta name="description" content="How to use DOS ex Machina: the C: drive, the controls on the case, SETUP, CATALOG, the monitor's OSD, MIDI, command-line flags and known gaps.">
<link rel="canonical" href="https://dosexmachina.com/manual.html">
<meta name="theme-color" media="(prefers-color-scheme: light)" content="#ddd5bf">
<meta name="theme-color" media="(prefers-color-scheme: dark)" content="#0d0e0d">
<meta property="og:type" content="article">
<meta property="og:site_name" content="DOS ex Machina">
<meta property="og:title" content="DOS ex Machina — User's Guide">
<meta property="og:url" content="https://dosexmachina.com/manual.html">
<meta property="og:image" content="https://dosexmachina.com/img/og.jpg">
<link rel="icon" href="icon-32.png" sizes="32x32" type="image/png">
<link rel="apple-touch-icon" href="icon-180.png">
<script>
try { var t = localStorage.getItem("dxm-lights"); if (t === "dark" || t === "light") document.documentElement.setAttribute("data-theme", t); } catch (e) {}
</script>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:ital,wdth,wght@0,100..125,500..800;1,100..125,500..800&family=IBM+Plex+Mono:wght@400;500;600&family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Serif:ital,wght@0,400;0,600;1,400&display=swap">
<link rel="stylesheet" href="site.css">
</head>
<body>
<!-- Made from MANUAL.md by tools/mksite.py.  Edit that, not this. -->

<header class="mast">
  <div class="wrap">
    <a class="brand" href="./">DOS <i>ex</i> Machina</a>
    <nav class="nav" aria-label="Site">
      <a href="./#panel">Front panel</a>
      <a href="./#spec">Specifications</a>
      <a href="./#get">Get it</a>
      <a href="manual.html" aria-current="page">User's Guide</a>
      <a href="https://github.com/pedrocatalao/dos-ex-machina">GitHub</a>
    </nav>
    <button class="lights" type="button" aria-pressed="false" title="Switch the room's lights">
      <span>Lights</span><span class="rocker" aria-hidden="true"></span>
    </button>
  </div>
</header>

<main>
"""

FOOT = """</main>

<footer>
  <div class="wrap">
    <div>
      <p>This guide is made from <a href="%(blob)sMANUAL.md">MANUAL.md</a> in the repository. If something in it is wrong, that is the file to fix.</p>
      <p class="fine">DOS ex Machina is free software under the GPL, version 2 or later. Horizon Computer Systems is fictional, and so is the part number.</p>
    </div>
    <nav aria-label="Elsewhere">
      <a href="./">Front page</a>
      <a href="https://github.com/pedrocatalao/dos-ex-machina">Source on GitHub</a>
      <a href="https://github.com/pedrocatalao/dos-ex-machina/issues">Report a problem</a>
      <a href="%(blob)sBUILDING.md">Building it</a>
    </nav>
  </div>
</footer>

<script src="site.js" defer></script>
</body>
</html>
""" % {"blob": BLOB}


def build():
    title, intro, chapters = parse(open(SRC, encoding="utf-8").read())
    page = [HEAD]
    page.append('<div class="guide-cover"><div class="wrap">\n')
    page.append('  <span class="label">Horizon Computer Systems &middot; Desktop series</span>\n')
    page.append("  <h1>User's Guide</h1>\n")
    for b in blocks(intro):
        page.append("  " + b.replace("<p>", '<p class="lede">', 1) + "\n")
    page.append('  <div class="part"><span>Part No. 2A4KD000C-00</span><span>%d chapters</span>'
                "<span>Keep this guide near the machine</span></div>\n" % len(chapters))
    page.append("</div></div>\n\n")

    page.append('<div class="wrap guide">\n')
    page.append('  <nav class="toc" aria-label="Contents">\n    <span class="label">Contents</span>\n    <ol>\n')
    for heading, _ in chapters:
        page.append('      <li><a href="#%s">%s</a></li>\n' % (slug(heading), inline(heading)))
    page.append("    </ol>\n  </nav>\n\n  <div>\n")

    fig = 0
    for n, (heading, lines) in enumerate(chapters, 1):
        anchor = slug(heading)
        page.append('    <article class="chapter" id="%s">\n' % anchor)
        page.append('      <header><span class="no" aria-hidden="true">%d</span><h2>%s</h2></header>\n'
                    % (n, inline(heading)))
        page.append('      <div class="prose">\n')
        body = blocks(lines)
        if anchor in FIGURES:
            fig += 1
            src, small, alt, cap = FIGURES[anchor]
            srcset = ' srcset="%s 960w, %s 1600w" sizes="(max-width: 900px) 100vw, 736px"' % (small, src) if small else ""
            figure = ('<figure><img src="%s"%s loading="lazy" alt="%s">'
                      '<p class="figcap"><b>Figure %d</b>%s</p></figure>'
                      % (src, srcset, html.escape(alt, quote=True), fig, html.escape(cap)))
            body.insert(1 if len(body) > 1 else len(body), figure)  # after the chapter's first block
        for b in body:
            page.append("        %s\n" % b)
        page.append("      </div>\n    </article>\n\n")
    page.append("  </div>\n</div>\n\n")
    page.append(FOOT)
    return "".join(page)


def main():
    made = build()
    if "--check" in sys.argv[1:]:
        have = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if have != made:
            sys.exit("docs/manual.html is not what tools/mksite.py makes from MANUAL.md")
        print("docs/manual.html: matches MANUAL.md")
        return
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(made)
    print("docs/manual.html: %d chapters" % made.count('<article class="chapter"'))


if __name__ == "__main__":
    main()
