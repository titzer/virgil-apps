#!/usr/bin/env python3
# Compare a Virgil Chroma* benchmark's dump output against its C++ reference.
# Usage: chroma_compare.py virgil.out ref.out
# Discrete lines (per-beat argmax/triad, root/triad counts) must match exactly;
# "V" chroma-vector lines are compared with a tolerance (max abs diff reported
# per algorithm).  Checksums are expected to differ (samples match to ~1 ulp).
import sys, re
def load(p):
    lines = open(p).read().splitlines()
    vec, disc = {}, []
    for l in lines:
        if l.startswith('V '):
            t = l.split(); vec[(int(t[1]), int(t[2]))] = [float(x) for x in t[3:]]
        else:
            disc.append(re.sub(r' checksum -?\d+', '', re.sub(r' cos [0-9.]+', '', l)))
    return vec, disc
va, da = load(sys.argv[1]); vb, db = load(sys.argv[2])
ok = True
mism = [(x, y) for x, y in zip(da, db) if x != y]
if len(da) != len(db) or mism:
    ok = False
    print("discrete output differs:", len(da), len(db))
    for x, y in mism[:10]: print("  virgil:", x, "\n  ref:   ", y)
else:
    print("discrete output (argmax/triad per beat, counts) identical: %d lines" % len(da))
algos = sorted(set(k[1] for k in va))
for a in algos:
    mx, at = 0.0, None
    for k in va:
        if k[1] != a: continue
        if k not in vb: ok = False; continue
        for i, (x, y) in enumerate(zip(va[k], vb[k])):
            if abs(x - y) > mx: mx, at = abs(x - y), (k[0], i)
    print("algo %d: max abs diff %.6f (beat, pc) = %s" % (a, mx, at))
sys.exit(0 if ok else 1)
