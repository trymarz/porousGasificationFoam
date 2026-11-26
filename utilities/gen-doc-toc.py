#!/usr/bin/env python3
"""gen-doc-toc.py — refresh the section links in doc/README.md, the table of contents.

The page lines ("- [label](page.md)") are written by hand; the indented section
links under each are regenerated from the page's "## " headings. --check: exit 1
if doc/README.md is stale.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOC = os.path.join(ROOT, "doc", "README.md")
PAGE = re.compile(r"^- \[[^\]]+\]\(([^)#\s]+\.md)\)")
GENERATED = {"pgf-reference.md"}  # sections are one per pgf help page; link the page only
SKIP = {"Related pages"}


def slug(heading):
    """GitHub's anchor for a heading."""
    t = re.sub(r"[^\w\- ]", "", heading.strip().lower())
    return t.replace(" ", "-")


def sections(page):
    """The "## " headings of a page, outside fenced code."""
    out, fenced = [], False
    with open(os.path.join(ROOT, "doc", page)) as f:
        for line in f:
            if line.startswith("```"):
                fenced = not fenced
            elif not fenced and line.startswith("## "):
                h = line[3:].strip()
                if h not in SKIP:
                    out.append(h)
    return out


def generate(text):
    out = []
    for line in text.split("\n"):
        if line.startswith("  - [") and out and (PAGE.match(out[-1]) or out[-1].startswith("  - [")):
            continue  # an old section link: regenerated below its page
        out.append(line)
        m = PAGE.match(line)
        if m and os.path.basename(m.group(1)) not in GENERATED:
            page = m.group(1)
            for h in sections(page):
                out.append(f"  - [{h}]({page}#{slug(h)})")
    return "\n".join(out)


def main():
    check = sys.argv[1:] == ["--check"]
    if sys.argv[1:] not in ([], ["--check"]):
        sys.exit("usage: utilities/gen-doc-toc.py [--check]")
    with open(TOC) as f:
        old = f.read()
    new = generate(old)
    if check:
        if new != old:
            print("doc/README.md is stale: run utilities/gen-doc-toc.py", file=sys.stderr)
            sys.exit(1)
        return
    with open(TOC, "w") as f:
        f.write(new)


if __name__ == "__main__":
    main()
