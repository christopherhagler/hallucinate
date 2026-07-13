#!/usr/bin/env python3
"""Assemble docs/book/building-an-os.md from the individual chapter files.

The combined file is a build artifact of the split chapters: a title page,
then README.md, the numbered chapters in order, and the appendices, each
separated by a page-break div for print/PDF rendering. Edit the chapter
files, never building-an-os.md; regenerate with `make book`.
"""

import pathlib
import sys

BOOK_DIR = pathlib.Path(__file__).resolve().parent.parent / "docs" / "book"
OUTPUT = BOOK_DIR / "building-an-os.md"
PAGE_BREAK = '<div style="page-break-after: always"></div>'
TITLE_PAGE = (
    "# Building an Operating System From Scratch\n"
    "\n"
    "*A complete walkthrough of Hallucinate OS, from the boot sector to a "
    "graph filesystem.*\n"
)


def chapter_files() -> list[pathlib.Path]:
    chapters = sorted(BOOK_DIR.glob("[0-9][0-9]-*.md"))
    appendices = sorted(BOOK_DIR.glob("appendix-*.md"))
    if not chapters or not appendices:
        sys.exit("mkbook: no chapter or appendix files found in " + str(BOOK_DIR))
    return [BOOK_DIR / "README.md"] + chapters + appendices


def main() -> None:
    parts = [TITLE_PAGE] + [p.read_text() for p in chapter_files()]
    sep = "\n" + PAGE_BREAK + "\n\n"
    body = sep.join(part.rstrip("\n") + "\n" for part in parts)
    OUTPUT.write_text(body + "\n" + PAGE_BREAK + "\n")
    print(f"mkbook: wrote {OUTPUT.relative_to(BOOK_DIR.parent.parent)} "
          f"({len(parts) - 1} sections)")


if __name__ == "__main__":
    main()
