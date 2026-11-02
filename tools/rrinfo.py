"""rrinfo.py FILE [RACER]: a replay file's header and a few samples of one racer"""
import struct, sys
from rrfile import read, sample
h, frames = read(sys.argv[1])
print({k: v for k, v in h.items() if k != 'racers'})
for i, r in enumerate(h['racers']): print(' racer', i, r)
print('bytes per racer-frame %.1f' % (h['packed'] / max(1, h['frames']) / 8))
r = int(sys.argv[2]) if len(sys.argv) > 2 else 0
for n in range(0, len(frames), max(1, len(frames) // 12)):
    t, rt = struct.unpack_from('<2f', frames[n]); v = sample(frames[n], r)
    print('%7.2f %7.2f pos %8.0f %8.0f %7.0f ang %6d %6d %6d fl %x rank %d lap %d btn %x item %d/%d res %.2f ghost %04x' % (
        t, rt, *[x / 16 for x in v[0:3]], *v[6:9], v[9], v[10], v[11], v[12], v[14], v[15], v[16] / 100, v[24]))
