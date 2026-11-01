#!/usr/bin/env python3
"""
Applies the three remaining Chromium edits for chrome://newpage.
Run from your Chromium `src` folder:

    python apply_newpage_patches.py

Safe to run twice (it skips edits that are already there). Review with `git diff` afterwards.
search.cc is NOT handled here: copy the provided search.cc over chrome\\browser\\search\\search.cc.
"""
import re
import sys
from pathlib import Path


def read(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def write(p, text):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def split(text):
    nl = "\r\n" if "\r\n" in text else "\n"
    return text.replace("\r\n", "\n").split("\n"), nl


def join(lines, nl):
    return nl.join(lines)


def indent_of(line):
    return line[: len(line) - len(line.lstrip())]


def show(lines, i, before=3, after=3):
    for k in range(max(0, i - before), min(len(lines), i + after + 1)):
        print(f"      {k + 1:6d}| {lines[k]}")


failed = []


# ---------------------------------------------------------------- 1. grd
def patch_grd():
    p = "chrome/browser/browser_resources.grd"
    if not Path(p).exists():
        failed.append(f"{p} not found (run from the src folder; or your tree uses another grd)")
        return
    text = read(p)
    if "IDR_NEWPAGE_INDEX_HTML" in text:
        print(f"[skip] {p}: already patched")
        return
    lines, nl = split(text)
    idx = next((i for i, l in enumerate(lines) if "</includes>" in l), None)
    if idx is None:
        failed.append(f"{p}: no </includes> found")
        return
    m = re.search(r'file="resources([\\/])', text)
    sep = m.group(1) if m else "\\"
    pad = "    "
    new = [
        f'{pad}<include name="IDR_NEWPAGE_INDEX_HTML" file="resources{sep}newpage{sep}index.html" type="BINDATA" />',
        f'{pad}<include name="IDR_NEWPAGE_JS" file="resources{sep}newpage{sep}newpage.js" type="BINDATA" />',
        f'{pad}<include name="IDR_NEWPAGE_CSS" file="resources{sep}newpage{sep}newpage.css" type="BINDATA" />',
        f'{pad}<include name="IDR_NEWPAGE_BRIDGE_JS" file="resources{sep}newpage{sep}bridge.js" type="BINDATA" />',
    ]
    lines[idx:idx] = new
    write(p, join(lines, nl))
    print(f"[ok]   {p}: added 4 <include> lines")
    show(lines, idx, 2, 6)


# ---------------------------------------------------------------- 2. BUILD.gn
NEWPAGE_SOURCES = [
    '"webui/newpage/newpage_constants.h",',
    '"webui/newpage/newpage_handler.cc",',
    '"webui/newpage/newpage_handler.h",',
    '"webui/newpage/newpage_ui.cc",',
    '"webui/newpage/newpage_ui.h",',
]


def strip_comment(line):
    return line.split("#", 1)[0]


def enclosing_list_opener(lines, idx):
    """Index of the line that opens the [...] list containing lines[idx], or None."""
    depth = 0
    for k in range(idx - 1, -1, -1):
        l = strip_comment(lines[k])
        depth += l.count("]") - l.count("[")
        if depth < 0:
            return k
    return None


def target_of(lines, idx):
    for k in range(idx, -1, -1):
        m = re.match(r'\s*(static_library|source_set|component)\("([^"]+)"\)', lines[k])
        if m:
            return m.group(2)
    return "?"


def patch_build_gn():
    p = "chrome/browser/ui/BUILD.gn"
    if not Path(p).exists():
        failed.append(f"{p} not found")
        return
    lines, nl = split(read(p))

    # 1) remove any earlier (possibly misplaced) insertion
    before = len(lines)
    # also catches the label-style form ("//chrome/browser/ui/webui/newpage/...") from a deps list
    lines = [l for l in lines
             if l.strip().replace('"//chrome/browser/ui/', '"') not in NEWPAGE_SOURCES]
    removed = before - len(lines)
    if removed:
        print(f"[fix]  {p}: removed {removed} previously inserted newpage line(s)")

    # 2) find a `webui/...` source line that sits in a `sources = [` / `sources += [` list
    anchors = ['"views/sidebar_apps/sidebar_view.cc"',  # your Dockly sources: known to be active in your build
               '"webui/new_tab_page/', '"webui/ntp/', '"webui/net_internals/',
               '"webui/management/', '"webui/nacl_ui', '"webui/metrics_handler']
    best = None
    for a in anchors:
        for i, l in enumerate(lines):
            if not l.strip().startswith(a):
                continue
            op = enclosing_list_opener(lines, i)
            if op is None or not re.match(r"\s*sources\s*\+?=\s*\[", lines[op]):
                continue
            tgt = target_of(lines, i)
            if tgt == "ui":
                best = (i, op, tgt)
                break
            if best is None:
                best = (i, op, tgt)
        if best and best[2] == "ui":
            break
    if best is None:
        write(p, join(lines, nl))
        failed.append(f"{p}: could not find a `sources = [` list with webui/ entries; add the 5 lines by hand (see PATCHES.md)")
        return

    idx, op, tgt = best
    pad = indent_of(lines[idx])
    lines[idx:idx] = [pad + s for s in NEWPAGE_SOURCES]
    write(p, join(lines, nl))
    print(f'[ok]   {p}: added 5 sources to "{lines[op].strip()}" in target "{tgt}"')
    if tgt != "ui":
        print('       WARNING: expected target "ui". Check the context below.')
    show(lines, op, 0, 0)
    print("       ...")
    show(lines, idx, 3, 8)


# ---------------------------------------------------------------- 3. factory
def patch_factory():
    p = "chrome/browser/ui/webui/chrome_web_ui_controller_factory.cc"
    if not Path(p).exists():
        failed.append(f"{p} not found")
        return
    text = read(p)
    if "NewWebUI<NewPageUI>" in text:
        fixed = text.replace("url.host_piece() == newpage::kHost", "url.host() == newpage::kHost")
        if fixed != text:
            write(p, fixed)
            print(f"[fix]  {p}: host_piece() -> host() (GURL in this tree has no host_piece)")
        else:
            print(f"[skip] {p}: already patched")
        return
    lines, nl = split(text)

    # includes: right after our own header (unconditional)
    own = next((i for i, l in enumerate(lines)
                if l.strip() == '#include "chrome/browser/ui/webui/chrome_web_ui_controller_factory.h"'), None)
    if own is None:
        own = next((i for i, l in enumerate(lines) if l.startswith('#include "chrome/browser/ui/webui/')), None)
    if own is None:
        failed.append(f"{p}: no include anchor found")
        return
    lines[own + 1:own + 1] = [
        '#include "chrome/browser/ui/webui/newpage/newpage_constants.h"',
        '#include "chrome/browser/ui/webui/newpage/newpage_ui.h"',
    ]

    # the if: first `if (url.host_piece() == ...` inside GetWebUIFactoryFunction
    fn = None
    for i, l in enumerate(lines):
        if re.search(r"\bGetWebUIFactoryFunction\(", l) and not l.lstrip().startswith(("//", "return")):
            window = " ".join(lines[i:i + 4])
            if "{" in window and ";" not in window.split("{")[0]:
                fn = i
                break
    if fn is None:
        failed.append(f"{p}: could not find the body of GetWebUIFactoryFunction; add the `if` by hand (see PATCHES.md)")
        return
    idx = next((i for i in range(fn + 1, len(lines))
                if re.match(r"\s*if \(url\.host_piece\(\) == ", lines[i])), None)
    if idx is None:
        failed.append(f"{p}: no `if (url.host_piece() == ...` found inside GetWebUIFactoryFunction")
        return
    pad = indent_of(lines[idx])
    lines[idx:idx] = [
        f"{pad}if (url.host() == newpage::kHost)",
        f"{pad}  return &NewWebUI<NewPageUI>;",
    ]
    write(p, join(lines, nl))
    print(f"[ok]   {p}: added 2 includes and the host check")
    show(lines, idx, 4, 6)


if not Path("chrome/browser").is_dir():
    sys.exit("Run this from the Chromium src folder (the one that contains chrome\\browser).")

patch_grd()
patch_build_gn()
patch_factory()

if failed:
    print("\nNEEDS MANUAL ATTENTION:")
    for f in failed:
        print("  -", f)
    sys.exit(1)
print("\nAll done. Review with: git diff --stat")
