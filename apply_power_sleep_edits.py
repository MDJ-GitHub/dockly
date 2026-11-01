"""Run from the Chromium `src` folder:  python apply_power_sleep_edits.py

Makes the two small edits in Chromium's own files that Power Sleep needs:
  1. chrome/browser/ui/sad_tab_helper.cc      -> no "Aw, Snap!" for Power Sleep tabs
  2. chrome/browser/ui/tabs/tab_renderer_data.cc -> no dead-tab icon, show the
     page icon in greyscale instead
Each edit is skipped if it is already there, and nothing is written if an anchor
cannot be found (the script then says which one).
"""
import glob
import os
import re
import sys

INCLUDE = '#include "chrome/browser/ui/views/sidebar_apps/power_sleep.h"\n'


def locate(path):
    """Returns |path|, or the same file name found somewhere else in the tree."""
    if os.path.exists(path):
        return path
    name = os.path.basename(path)
    found = [p for p in glob.glob("chrome/browser/ui/**/" + name, recursive=True)
             + glob.glob("chrome/browser/**/" + name, recursive=True)
             + glob.glob("components/**/" + name, recursive=True)]
    found = sorted(set(found))
    if len(found) == 1:
        print(f"[info] {path} is at {found[0]} in this tree")
        return found[0]
    print(f"[FAIL] {path}: not found" if not found else
          f"[FAIL] {path}: several candidates, tell me which is the right one: {found}")
    return None


def patch(path, include_after, anchor_regex, insert):
    path = locate(path)
    if not path:
        return False
    with open(path, encoding="utf-8", newline="") as f:
        src = f.read()
    if "power_sleep.h" in src:
        print(f"[skip] {path}: already patched")
        return True
    nl = "\r\n" if "\r\n" in src else "\n"
    src = src.replace("\r\n", "\n")

    inc = re.search(r'^#include "' + re.escape(include_after) + r'"\n', src, re.M)
    if not inc:
        includes = list(re.finditer(r'^#include .*\n', src, re.M))
        if not includes:
            print(f"[FAIL] {path}: no #include lines found")
            return False
        inc = includes[-1]
    m = re.search(anchor_regex, src, re.M)
    if not m:
        print(f"[FAIL] {path}: code anchor not found: {anchor_regex}")
        return False

    # Insert the code first (it is later in the file), then the include.
    src = src[: m.end()] + insert + src[m.end():]
    src = src[: inc.end()] + INCLUDE + src[inc.end():]
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(src.replace("\n", nl))
    print(f"[ok]   {path}")
    return True


ok = True

ok &= patch(
    "chrome/browser/ui/sad_tab_helper.cc",
    "chrome/browser/ui/sad_tab.h",
    r"^  if \(browser_shutdown::HasShutdownStarted\(\)\) \{\n    return;\n  \}\n",
    "\n"
    "  // Power Sleep ended this renderer on purpose: not a crash, so no sad tab.\n"
    "  // The tab reloads by itself when it is shown.\n"
    "  if (sidebar_apps::IsPowerSlept(web_contents())) {\n"
    "    return;\n"
    "  }\n",
)

ok &= patch(
    "chrome/browser/ui/tabs/tab_renderer_data.cc",
    "chrome/browser/ui/tab_ui_helper.h",
    r"^  data\.crashed_status = contents->GetCrashedStatus\(\);\n",
    "\n"
    "  // Power Sleep: not a crash. No dead-tab icon; the page's own icon, grey.\n"
    "  if (sidebar_apps::IsPowerSlept(contents)) {\n"
    "    data.crashed_status = base::TERMINATION_STATUS_STILL_RUNNING;\n"
    "    data.favicon = sidebar_apps::PowerSleepFavicon(data.favicon);\n"
    "  }\n",
)

sys.exit(0 if ok else 1)
