# Run from your Chromium "src" folder:
#   python3 find_free_resource_id.py
# Prints free id gaps in tools/gritsettings/resource_ids.spec and the exact
# entry to paste for the Dockium welcome page.
import sys

SPEC = sys.argv[1] if len(sys.argv) > 1 else r'tools/gritsettings/resource_ids.spec'
NEED = 20  # ids we reserve (our page uses 7)

with open(SPEC, encoding='utf-8') as f:
    spec = eval(f.read(), {})

used = []  # (start, end, owner)
for owner, entry in spec.items():
    if not isinstance(entry, dict):
        continue
    sizes = entry.get('META', {}).get('sizes', {})
    for kind, ids in entry.items():
        if kind == 'META' or not isinstance(ids, list):
            continue
        for n, start in enumerate(ids):
            try:
                size = sizes[kind][n]
            except (KeyError, IndexError):
                size = 1  # unknown size: be conservative about the start only
            used.append((start, start + size, owner))

used.sort()
gaps = []
for (s1, e1, o1), (s2, _, o2) in zip(used, used[1:]):
    if s2 - e1 >= NEED + 10:
        gaps.append((e1, s2, o1))

print('Entries found:', len(used))
print('Free gaps (>= %d ids), first 8:' % (NEED + 10))
for e1, s2, owner in gaps[:8]:
    print('  %d .. %d   (after %s)' % (e1, s2, owner))

if not gaps:
    print('No gap found. Send me the last 40 lines of resource_ids.spec and I will pick one.')
    sys.exit(1)

# Prefer a gap next to other chrome/browser/resources entries; else the first gap.
near = [g for g in gaps if 'chrome/browser/resources' in g[2]]
e1, s2, owner = (near or gaps)[-1] if near else gaps[0]
pick = e1 + 5
print()
print('Paste this entry into resource_ids.spec right AFTER the entry for:')
print('  ' + owner)
print()
print('  "<(SHARED_INTERMEDIATE_DIR)/chrome/browser/resources/dockium_welcome/dockium_welcome_resources.grd": {')
print('    "META": {"sizes": {"includes": [%d]}},' % NEED)
print('    "includes": [%d],' % pick)
print('  },')
