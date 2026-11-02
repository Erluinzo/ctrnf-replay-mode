"""rrscan.py FILE: every change of item, charges, uses, horn, damage and respawn per racer"""
import struct, sys
from rrfile import read, sample
h, frames = read(sys.argv[1])
last = {}
for n, f in enumerate(frames):
    t = struct.unpack_from('<f', f)[0]
    for r in range(8):
        v = sample(f, r)
        key = (v[14], v[15], v[26], v[21], v[9] & 32, v[20], v[25])
        if last.get(r) != key:
            if n: print('%6.2f racer %d item %d x%d uses %d juiced %d wumpa %d honk %d damage %d respawn %d' % (t, r, v[14], v[15], v[26], v[21], v[18], v[9] >> 5 & 1, v[20] - 1, v[25]))
            last[r] = key
