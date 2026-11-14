#!/usr/bin/env python3
"""Prove that the controller split (plan step 2.4) was a mechanical move.

Extracts every `PlantOSController::name(...) { ... }` body from the unsplit controller.cpp
and from all .cpp files of the split component, and compares them ignoring whitespace.

    git show ac2b66a:components/plantos_controller/controller.cpp > /tmp/base.cpp
    python3 tests/tools/bodydiff.py /tmp/base.cpp components/plantos_controller [-v]
"""
import sys, re, glob, difflib

start_re = re.compile(r'^[A-Za-z].*PlantOSController::(~?\w+)\(.*\{\s*$')


def bodies(path):
    lines = open(path).read().split('\n')
    found = {}
    i = 0
    while i < len(lines):
        m = start_re.match(lines[i])
        if m:
            j = i
            while lines[j] != '}':
                j += 1
            assert m.group(1) not in found, m.group(1)
            found[m.group(1)] = (path.split('/')[-1], lines[i:j + 1])
            i = j
        i += 1
    return found


def norm(ls):
    return [re.sub(r'\s+', '', l) for l in ls if l.strip()]


old = bodies(sys.argv[1])
new = {}
for f in sorted(glob.glob(sys.argv[2] + '/*.cpp')):
    for k, v in bodies(f).items():
        assert k not in new, k
        new[k] = v

same = 0
for k in old:
    if k not in new:
        print("MISSING in split files:", k)
        continue
    a, b = norm(old[k][1]), norm(new[k][1])
    if a == b:
        same += 1
        continue
    d = [x for x in difflib.unified_diff(a, b, lineterm='', n=0) if not x.startswith(('---', '+++', '@@'))]
    print(f"DIFF {k} ({new[k][0]}): -{sum(x[0] == '-' for x in d)} +{sum(x[0] == '+' for x in d)} lines")
    if '-v' in sys.argv:
        for x in d:
            print("   ", x[:170])
for k in new:
    if k not in old:
        print("NEW (not in base):", k, new[k][0])
print(f"{len(old)} functions in base, {len(new)} in split files, {same} identical bodies")
