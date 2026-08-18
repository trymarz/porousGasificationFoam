#!/usr/bin/env python3
"""Build a browsable view of the PNGs generated for tutorial cases.

The PNGs stay next to the case that produced them. This script creates the
ignored ``tutorials/render/`` view, with one folder and HTML page per case,
plus a grouped landing page for finding cases quickly.
"""

from __future__ import annotations

import html
import os
import shutil
from collections import defaultdict
from pathlib import Path
from urllib.parse import quote


TUTORIALS_DIR = Path(__file__).resolve().parent
CASES_DIR = TUTORIALS_DIR / "cases"
GALLERY_DIR = TUTORIALS_DIR / "render"


def discover_images() -> list[tuple[Path, list[Path]]]:
    """Return case render directories that contain PNG files."""
    discovered = []
    for render_dir in sorted(CASES_DIR.rglob("render")):
        if not render_dir.is_dir():
            continue
        images = sorted(path for path in render_dir.rglob("*.png") if path.is_file())
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


def case_relative_path(render_dir: Path) -> Path:
    return render_dir.parent.relative_to(CASES_DIR)


def case_gallery_dir(render_dir: Path) -> Path:
    return GALLERY_DIR / "cases" / case_relative_path(render_dir)


def create_mirrors(render_dirs: list[tuple[Path, list[Path]]]) -> None:
    """Create an image symlink inside each generated case folder."""
    for render_dir, _ in render_dirs:
        case_dir = case_gallery_dir(render_dir)
        case_dir.mkdir(parents=True, exist_ok=True)
        target = os.path.relpath(render_dir, case_dir)
        (case_dir / "images").symlink_to(target, target_is_directory=True)


def url_path(path: Path) -> str:
    """Return a browser-safe URL for a relative path."""
    return quote(path.as_posix(), safe="/:@-._~")


def case_url(case_rel: Path) -> str:
    return url_path(Path("cases") / case_rel / "index.html")


def image_url(case_rel: Path, image: Path) -> str:
    image_rel = image.relative_to(CASES_DIR / case_rel / "render")
    return url_path(Path("cases") / case_rel / "images" / image_rel)


def case_label(case_rel: Path) -> str:
    return case_rel.as_posix()


def case_card_html(render_dir: Path, images: list[Path]) -> str:
    """Render one case preview for the landing page."""
    case_rel = case_relative_path(render_dir)
    group = case_rel.parts[0]
    name = case_rel.name
    label = case_label(case_rel)
    preview = image_url(case_rel, images[0])
    return f"""\
      <article class="case-card" data-search="{html.escape(label + ' ' + images[0].name, quote=True)}"
               data-group="{html.escape(group, quote=True)}">
        <a href="{html.escape(case_url(case_rel), quote=True)}">
          <img loading="lazy" src="{html.escape(preview, quote=True)}"
               alt="Preview of {html.escape(label, quote=True)}">
          <div class="case-card-body">
            <span class="eyebrow">{html.escape(group)}</span>
            <h3>{html.escape(name)}</h3>
            <p>{len(images)} snapshot{'s' if len(images) != 1 else ''}</p>
            <span class="open-link">Open case -&gt;</span>
          </div>
        </a>
      </article>
"""


def case_image_card_html(case_rel: Path, image: Path) -> str:
    """Render one image card for a case page."""
    image_rel = image.relative_to(CASES_DIR / case_rel / "render").as_posix()
    label = f"{case_label(case_rel)} / {image_rel}"
    escaped_url = html.escape(url_path(Path("images") / Path(image_rel)), quote=True)
    escaped_label = html.escape(label)
    return f"""\
      <button class="image-card" type="button"
              data-search="{html.escape(image_rel, quote=True)}"
              data-src="{escaped_url}" data-label="{html.escape(label, quote=True)}">
        <img loading="lazy" src="{escaped_url}" alt="{escaped_label}">
        <span>{html.escape(image_rel)}</span>
      </button>
"""


def common_styles() -> str:
    return """
    :root { color-scheme: dark; --bg: #0b1120; --panel: #111827; --panel-hi: #182338;
      --line: #293548; --text: #e5e7eb; --muted: #94a3b8; --accent: #67e8f9; }
    * { box-sizing: border-box; }
    body { background: var(--bg); color: var(--text); font: 15px/1.45 system-ui, sans-serif;
      margin: 0; padding: 2rem; }
    a { color: inherit; text-decoration: none; }
    header, main { margin: 0 auto; max-width: 1500px; }
    header { margin-bottom: 1.5rem; }
    h1, h2, h3, p { margin: 0; }
    h1 { font-size: clamp(1.6rem, 3vw, 2.5rem); letter-spacing: -.03em; }
    h2 { font-size: 1rem; }
    .subhead { color: var(--muted); margin-top: .35rem; }
    .toolbar { align-items: center; display: flex; flex-wrap: wrap; gap: .65rem; margin: 1.25rem 0; }
    input, select, .toolbar button { background: var(--panel); border: 1px solid var(--line);
      border-radius: .45rem; color: inherit; font: inherit; padding: .6rem .75rem; }
    input { min-width: min(32rem, 80vw); }
    button { cursor: pointer; }
    .toolbar button:hover, select:focus, input:focus { border-color: var(--accent); outline: none; }
    .count { color: var(--muted); margin-left: auto; white-space: nowrap; }
    .groups { display: grid; gap: 1rem; }
    .group { background: color-mix(in srgb, var(--panel) 82%, transparent); border: 1px solid var(--line);
      border-radius: .8rem; overflow: hidden; }
    .group summary { align-items: center; cursor: pointer; display: flex; gap: .7rem; list-style: none;
      padding: 1rem 1.1rem; }
    .group summary::-webkit-details-marker { display: none; }
    .group summary::before { color: var(--accent); content: "+"; font-size: 1.2rem; width: 1rem; }
    .group[open] summary::before { content: "-"; }
    .group-meta { color: var(--muted); font-size: .85rem; margin-left: auto; }
    .case-grid, .image-grid { display: grid; gap: 1rem; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
      padding: 0 1rem 1rem; }
    .case-card { background: var(--panel); border: 1px solid var(--line); border-radius: .65rem; overflow: hidden;
      transition: border-color .15s, transform .15s; }
    .case-card:hover { border-color: var(--accent); transform: translateY(-2px); }
    .case-card img { background: #030712; display: block; height: 150px; object-fit: contain; width: 100%; }
    .case-card-body { padding: .8rem; }
    .eyebrow { color: var(--accent); font-size: .72rem; letter-spacing: .08em; text-transform: uppercase; }
    .case-card h3 { font-size: 1rem; margin-top: .2rem; overflow-wrap: anywhere; }
    .case-card p, .open-link { color: var(--muted); font-size: .82rem; }
    .open-link { display: block; margin-top: .55rem; }
    .back { color: var(--accent); display: inline-block; margin-bottom: 1rem; }
    .image-card { background: var(--panel); border: 1px solid var(--line); border-radius: .6rem; color: inherit;
      overflow: hidden; padding: 0; text-align: left; transition: border-color .15s, transform .15s; }
    .image-card:hover, .image-card:focus { border-color: var(--accent); outline: none; transform: translateY(-2px); }
    .image-card img { background: #030712; display: block; height: 220px; object-fit: contain; width: 100%; }
    .image-card span { color: var(--muted); display: block; font-family: ui-monospace, monospace; font-size: .75rem;
      overflow-wrap: anywhere; padding: .65rem; }
    .empty { color: var(--muted); padding: 2rem 0; }
    [hidden] { display: none !important; }
    .lightbox { align-items: center; background: rgb(3 7 18 / .9); display: flex; inset: 0; justify-content: center;
      padding: 3rem; position: fixed; z-index: 10; }
    .lightbox-content { max-height: 100%; max-width: min(1200px, 96vw); position: relative; text-align: center; }
    .lightbox img { max-height: 82vh; max-width: 90vw; object-fit: contain; }
    .lightbox-label { color: var(--text); font-family: ui-monospace, monospace; font-size: .8rem; margin-top: .6rem; }
    .lightbox-close, .lightbox-nav { background: var(--panel-hi); border: 1px solid var(--line); color: var(--text);
      border-radius: .4rem; cursor: pointer; font-size: 1.3rem; padding: .35rem .7rem; position: fixed; }
    .lightbox-close { right: 1rem; top: 1rem; }
    .lightbox-nav { top: 50%; transform: translateY(-50%); }
    .lightbox-prev { left: 1rem; }
    .lightbox-next { right: 1rem; }
    @media (max-width: 600px) { body { padding: 1rem; } .count { margin-left: 0; width: 100%; }
      .lightbox { padding: 1rem; } .lightbox-nav { bottom: 1rem; top: auto; transform: none; } }
"""


def write_case_index(render_dir: Path, images: list[Path]) -> None:
    """Write the interactive page for one case."""
    case_rel = case_relative_path(render_dir)
    case_name = case_label(case_rel)
    root_url = "../" * (len(case_rel.parts) + 1) + "index.html"
    cards = "".join(case_image_card_html(case_rel, image) for image in images)
    content = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{html.escape(case_name)} - porousGasificationFoam gallery</title>
  <style>{common_styles()}</style>
</head>
<body>
  <header>
    <a class="back" href="{html.escape(root_url, quote=True)}">&lt;- All cases</a>
    <h1>{html.escape(case_name)}</h1>
    <p class="subhead">{len(images)} generated snapshots</p>
    <div class="toolbar">
      <input id="filter" type="search" placeholder="Filter by field, slice, or time...">
      <span class="count" id="count"></span>
    </div>
  </header>
  <main>
    <section class="image-grid" id="grid">{cards}</section>
    <p class="empty" id="empty" hidden>No snapshots match this filter.</p>
  </main>
  <div class="lightbox" id="lightbox" hidden role="dialog" aria-modal="true" aria-label="Image viewer">
    <button class="lightbox-close" id="close" type="button" aria-label="Close">x</button>
    <button class="lightbox-nav lightbox-prev" id="prev" type="button" aria-label="Previous image">&lt;</button>
    <div class="lightbox-content"><img id="lightbox-image" alt=""><div class="lightbox-label" id="lightbox-label"></div></div>
    <button class="lightbox-nav lightbox-next" id="next" type="button" aria-label="Next image">&gt;</button>
  </div>
  <script>
    const filter = document.querySelector('#filter');
    const cards = [...document.querySelectorAll('.image-card')];
    const count = document.querySelector('#count');
    const empty = document.querySelector('#empty');
    const lightbox = document.querySelector('#lightbox');
    const lightboxImage = document.querySelector('#lightbox-image');
    const lightboxLabel = document.querySelector('#lightbox-label');
    let visibleCards = [];
    let current = 0;
    function update() {{
      const query = filter.value.trim().toLowerCase();
      visibleCards = cards.filter(card => card.dataset.search.toLowerCase().includes(query));
      cards.forEach(card => card.hidden = !visibleCards.includes(card));
      count.textContent = `${{visibleCards.length}} of ${{cards.length}} snapshots`;
      empty.hidden = visibleCards.length !== 0;
    }}
    function show(index) {{
      if (!visibleCards.length) return;
      current = (index + visibleCards.length) % visibleCards.length;
      const card = visibleCards[current];
      lightboxImage.src = card.dataset.src;
      lightboxImage.alt = card.dataset.label;
      lightboxLabel.textContent = `${{current + 1}} / ${{visibleCards.length}}  ${{card.dataset.label}}`;
    }}
    function open(card) {{ lightbox.hidden = false; show(visibleCards.indexOf(card)); document.querySelector('#close').focus(); }}
    function close() {{ lightbox.hidden = true; }}
    filter.addEventListener('input', update);
    cards.forEach(card => card.addEventListener('click', () => open(card)));
    document.querySelector('#close').addEventListener('click', close);
    document.querySelector('#prev').addEventListener('click', () => show(current - 1));
    document.querySelector('#next').addEventListener('click', () => show(current + 1));
    document.addEventListener('keydown', event => {{
      if (lightbox.hidden) return;
      if (event.key === 'Escape') close();
      if (event.key === 'ArrowLeft') show(current - 1);
      if (event.key === 'ArrowRight') show(current + 1);
    }});
    update();
  </script>
</body>
</html>
"""
    (case_gallery_dir(render_dir) / "index.html").write_text(content, encoding="utf-8")


def write_index(render_dirs: list[tuple[Path, list[Path]]]) -> int:
    """Write the grouped landing page and return the number of images."""
    groups: dict[str, list[tuple[Path, list[Path]]]] = defaultdict(list)
    image_count = 0
    for render_dir, images in render_dirs:
        groups[case_relative_path(render_dir).parts[0]].append((render_dir, images))
        image_count += len(images)

    sections = []
    for group, cases in sorted(groups.items()):
        cards = "".join(case_card_html(render_dir, images) for render_dir, images in cases)
        sections.append(f"""\
    <details class="group" data-group="{html.escape(group, quote=True)}" open>
      <summary><h2>{html.escape(group)}</h2><span class="group-meta">{len(cases)} cases</span></summary>
      <div class="case-grid">{cards}</div>
    </details>
""")

    options = "".join(
        f'<option value="{html.escape(group, quote=True)}">{html.escape(group)}</option>'
        for group in sorted(groups)
    )
    content = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>porousGasificationFoam render gallery</title>
  <style>{common_styles()}</style>
</head>
<body>
  <header>
    <h1>porousGasificationFoam render gallery</h1>
    <p class="subhead">Browse generated snapshots by tutorial case.</p>
    <div class="toolbar">
      <input id="filter" type="search" placeholder="Filter by case or snapshot name...">
      <select id="group-filter" aria-label="Filter by case group">
        <option value="">All groups</option>{options}
      </select>
      <button id="expand" type="button">Expand all</button>
      <button id="collapse" type="button">Collapse all</button>
      <span class="count" id="count"></span>
    </div>
  </header>
  <main class="groups" id="groups">{"".join(sections)}</main>
  <script>
    const filter = document.querySelector('#filter');
    const groupFilter = document.querySelector('#group-filter');
    const groups = [...document.querySelectorAll('.group')];
    const count = document.querySelector('#count');
    const allCases = groups.flatMap(group => [...group.querySelectorAll('.case-card')]);
    function update() {{
      const query = filter.value.trim().toLowerCase();
      const selectedGroup = groupFilter.value;
      let visible = 0;
      for (const group of groups) {{
        const groupMatches = !selectedGroup || group.dataset.group === selectedGroup;
        let groupVisible = 0;
        for (const card of group.querySelectorAll('.case-card')) {{
          const shown = groupMatches && (!query || card.dataset.search.toLowerCase().includes(query));
          card.hidden = !shown;
          if (shown) {{ groupVisible += 1; visible += 1; }}
        }}
        group.hidden = groupVisible === 0;
      }}
      count.textContent = `${{visible}} of ${{allCases.length}} cases`;
    }}
    function setGroups(open) {{ groups.forEach(group => group.open = open); }}
    filter.addEventListener('input', update);
    groupFilter.addEventListener('change', update);
    document.querySelector('#expand').addEventListener('click', () => setGroups(true));
    document.querySelector('#collapse').addEventListener('click', () => setGroups(false));
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
    for render_dir, images in render_dirs:
        write_case_index(render_dir, images)
    image_count = write_index(render_dirs)
    print(
        f"Generated {GALLERY_DIR} from {len(render_dirs)} cases "
        f"and {image_count} PNGs."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
