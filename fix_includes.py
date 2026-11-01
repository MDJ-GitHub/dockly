#!/usr/bin/env python3
"""
Checks every #include in the newpage C++ files, and for any header that does not exist in your
tree, looks for where it moved (same file name elsewhere, or the file that now defines the
symbol) and rewrites the include. Prints a report so you see all problems at once instead of
one per 15-second build.

Run from your Chromium `src` folder:

    python fix_includes.py            # uses out/Dev for generated headers
    python fix_includes.py out/Debug
"""
import re
import subprocess
import sys
from pathlib import Path

OUT = Path(sys.argv[1] if len(sys.argv) > 1 else "out/Dev")
DIR = Path("chrome/browser/ui/webui/newpage")
FILES = [DIR / n for n in ("newpage_constants.h", "newpage_ui.h", "newpage_ui.cc",
                           "newpage_handler.h", "newpage_handler.cc")]

# If a header vanished under its old name, which symbol is it that we need?
SYMBOLS = {
    "open_url_params.h": r"struct\s+(?:CONTENT_EXPORT\s+)?OpenURLParams\b",
    "referrer.h": r"struct\s+(?:CONTENT_EXPORT\s+)?Referrer\b",
    "web_ui_message_handler.h": r"class\s+(?:CONTENT_EXPORT\s+)?WebUIMessageHandler\b",
    "web_ui_data_source.h": r"class\s+(?:CONTENT_EXPORT\s+)?WebUIDataSource\b",
    "simple_url_loader.h": r"class\s+(?:COMPONENT_EXPORT\([A-Z_]+\)\s+)?SimpleURLLoader\b",
}


def git(*args):
    r = subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8")
    return [l.strip() for l in r.stdout.splitlines() if l.strip()]


def exists(inc):
    return Path(inc).exists() or (OUT / "gen" / inc).exists()


def find_new_home(inc):
    base = Path(inc).name
    hits = git("ls-files", "--", f"*/{base}", base)
    hits = [h for h in hits if not h.startswith(("third_party/", "docs/")) and h != inc]
    # prefer the same top-level dir (content/, base/, ui/ ...)
    top = inc.split("/", 1)[0]
    pref = [h for h in hits if h.startswith(top + "/")] or hits
    if len(pref) == 1:
        return pref[0]
    if len(pref) > 1:
        return ("ambiguous", pref)
    pat = SYMBOLS.get(base)
    if pat:
        rx = re.compile(pat)
        g = []
        for h in git("ls-files", "--", f"{top}/*.h"):
            try:
                if rx.search(Path(h).read_text(encoding="utf-8", errors="ignore")):
                    g.append(h)
            except OSError:
                pass
        g = [h for h in g if "/public/" in h or h.startswith(("base/", "ui/", "url/"))] or g
        if len(g) == 1:
            return g[0]
        if len(g) > 1:
            return ("ambiguous", g)
    return None


if not DIR.is_dir():
    sys.exit("Run this from the Chromium src folder.")

include_re = re.compile(r'^(\s*#include\s+")([^"]+)(")', re.M)
missing = {}
for f in FILES:
    for m in include_re.finditer(f.read_text(encoding="utf-8")):
        inc = m.group(2)
        if inc not in missing and not exists(inc):
            missing[inc] = None

if not missing:
    print("All includes resolve. Nothing to fix.")
    sys.exit(0)

repl, unresolved = {}, []
for inc in missing:
    res = find_new_home(inc)
    if isinstance(res, str):
        repl[inc] = res
        print(f"[moved]      {inc}\n             -> {res}")
    elif res:
        unresolved.append(inc)
        print(f"[ambiguous]  {inc}: candidates: {', '.join(res[1])}")
    else:
        unresolved.append(inc)
        print(f"[not found]  {inc}")

for f in FILES:
    text = f.read_text(encoding="utf-8")
    new = text
    for old, newinc in repl.items():
        new = new.replace(f'#include "{old}"', f'#include "{newinc}"')
    if new != text:
        f.write_text(new, encoding="utf-8")
        print(f"updated {f}")

if unresolved:
    print("\nCould not resolve:", *unresolved, sep="\n  ")
    print("Paste this output to me and I'll tell you the replacement.")
    sys.exit(1)
