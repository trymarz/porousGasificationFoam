#!/usr/bin/env python3
"""Build a browsable view of the PNGs generated for tutorial cases.

The PNGs stay next to the case that produced them.  This script creates the
ignored ``tutorials/render/`` view, which contains relative symlinks back to
those case-local render directories and a single HTML gallery.
"""

from __future__ import annotations

import html
import os
import shutil
from pathlib import Path
from urllib.parse import quote


TUTORIALS_DIR = Path(__file__).resolve().parent
CASES_DIR = TUTORIALS_DIR / "cases"
GALLERY_DIR = TUTORIALS_DIR / "render"


def discover_images() -> list[tuple[Path, list[Path]]]:
    """Return case-relative render directories that contain PNG files."""
    discovered = []
    for render_dir in sorted(CASES_DIR.rglob("render")):
        if not render_dir.is_dir():
            continue
        images = sorted(
            path
            for path in render_dir.rglob("*.png")
            if path.is_file()
        )
        if images:
            discovered.append((render_dir, images))
    return discovered


def reset_gallery() -> None:
    """Remove only the generated gallery root."""
    if GALLERY_DIR.is_symlink() or GALLERY_DIR.is_file():
        GALLERY_DIR.unlink()
    elif GALLERY_DIR.is_dir():
        shutil.rmtree(GALLERY_DIR)
    GALLERY_DIR.mkdir()


def create_mirrors(render_dirs: list[tuple[Path, list[Path]]]) -> None:
    """Mirror each case's render directory with a relative symlink."""
    for render_dir, _ in render_dirs:
        case_rel = render_dir.parent.relative_to(CASES_DIR)
        mirror = GALLERY_DIR / case_rel
        mirror.parent.mkdir(parents=True, exist_ok=True)
        target = os.path.relpath(render_dir, mirror.parent)
        mirror.symlink_to(target, target_is_directory=True)


def image_url(image: Path) -> str:
    """Return a browser-safe URL from the gallery to a source PNG."""
    relative = image.relative_to(TUTORIALS_DIR).as_posix()
    return "../" + quote(relative, safe="/:@-._~")


def card_html(render_dir: Path, image: Path) -> str:
    """Render one gallery card."""
    case_rel = render_dir.parent.relative_to(CASES_DIR).as_posix()
    image_rel = image.relative_to(render_dir).as_posix()
    label = f"{case_rel} / {image_rel}"
    escaped_url = html.escape(image_url(image), quote=True)
    escaped_label = html.escape(label)
    escaped_case = html.escape(case_rel, quote=True)
    escaped_image = html.escape(image_rel, quote=True)
    return f"""\
      <article class="card" data-search="{escaped_case} {escaped_image}">
        <a href="{escaped_url}" target="_blank" rel="noopener">
          <img loading="lazy" src="{escaped_url}" alt="{escaped_label}">
        </a>
        <div class="label">{escaped_label}</div>
      </article>
"""


def write_index(render_dirs: list[tuple[Path, list[Path]]]) -> int:
    """Write the gallery page and return the number of images."""
    cards = []
    image_count = 0
    for render_dir, images in render_dirs:
        for image in images:
            cards.append(card_html(render_dir, image))
            image_count += 1

    content = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>porousGasificationFoam render gallery</title>
  <style>
    :root { color-scheme: dark; }
    body {
      background: #111827;
      color: #e5e7eb;
      font: 15px/1.4 system-ui, sans-serif;
      margin: 0;
      padding: 1.5rem;
    }
    header {
      align-items: baseline;
      display: flex;
      flex-wrap: wrap;
      gap: 1rem;
      justify-content: space-between;
      margin: 0 auto 1.25rem;
      max-width: 1800px;
    }
    h1 { font-size: 1.35rem; margin: 0; }
    input {
      background: #1f2937;
      border: 1px solid #4b5563;
      border-radius: .35rem;
      color: inherit;
      font: inherit;
      min-width: min(32rem, 80vw);
      padding: .55rem .7rem;
    }
    #count { color: #9ca3af; white-space: nowrap; }
    .grid {
      display: grid;
      gap: 1rem;
      grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
      margin: 0 auto;
      max-width: 1800px;
    }
    .card {
      background: #1f2937;
      border: 1px solid #374151;
      border-radius: .5rem;
      overflow: hidden;
    }
    .card img {
      background: #030712;
      display: block;
      height: 220px;
      object-fit: contain;
      width: 100%;
    }
    .label {
      color: #d1d5db;
      font-family: ui-monospace, monospace;
      font-size: .78rem;
      overflow-wrap: anywhere;
      padding: .65rem;
    }
    .empty { color: #9ca3af; grid-column: 1 / -1; }
  </style>
</head>
<body>
  <header>
    <h1>porousGasificationFoam render gallery</h1>
    <input id="filter" type="search" placeholder="Filter by case, field, or time...">
    <span id="count"></span>
  </header>
  <main class="grid">
""" + "".join(cards) + """  </main>
  <script>
    const filter = document.querySelector('#filter');
    const cards = [...document.querySelectorAll('.card')];
    const count = document.querySelector('#count');
    function update() {
      const query = filter.value.trim().toLowerCase();
      let visible = 0;
      for (const card of cards) {
        const shown = !query || card.dataset.search.toLowerCase().includes(query);
        card.hidden = !shown;
        if (shown) visible += 1;
      }
      count.textContent = `${visible} of ${cards.length} images`;
    }
    filter.addEventListener('input', update);
    update();
  </script>
</body>
</html>
"""
    (GALLERY_DIR / "index.html").write_text(content, encoding="utf-8")
    return image_count


def main() -> int:
    if not CASES_DIR.is_dir():
        raise SystemExit(f"cases directory not found: {CASES_DIR}")

    render_dirs = discover_images()
    reset_gallery()
    create_mirrors(render_dirs)
    image_count = write_index(render_dirs)
    print(
        f"Generated {GALLERY_DIR} from {len(render_dirs)} cases "
        f"and {image_count} PNGs."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
