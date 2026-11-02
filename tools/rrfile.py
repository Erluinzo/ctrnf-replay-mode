"""rrfile.py: reads a Race Replay file (src/replay.h, packing as src/files.c; versions 4 and 5, velocity 0 in version 4). read(path) -> header dict, frames (bytes)"""
import struct
H = '<8s4I2f2i64s32s8s'
SAMPLE = '<3i3h3hHBBIhbBHHBBBBHHHBB'       # struct rr_sample, 52 bytes
FRAME = 8 + 8 * 52


def read(path):
    d = open(path, 'rb').read()
    magic, version, hsize, fsize, frames, go, dur, mode, laps, level, player, date = struct.unpack_from(H, d)
    off = struct.calcsize(H)
    racers = [struct.unpack_from('<32s32s32sbBB13sf', d, off + 116 * i) for i in range(8)]
    packed = struct.unpack_from('<I', d, off + 116 * 8)[0]
    track = d[off + 116 * 8 + 4:off + 116 * 8 + 44].split(b'\0')[0].decode(errors='replace')
    h = dict(version=version, frames=frames, go=go, duration=dur, mode=mode, laps=laps, track=track,
             level=level.split(b'\0')[0].decode(), player=player.split(b'\0')[0].decode(), date=list(date[:6]),
             racers=[dict(name=r[0].split(b'\0')[0].decode(), driver=r[1].split(b'\0')[0].decode(), kart=r[2].split(b'\0')[0].decode(),
                          id=r[3], kind=r[4], rank=r[5], finish=r[7]) for r in racers], packed=packed, size=len(d))
    assert magic == b'CTRRPLAY' and version in (4, 5) and fsize == FRAME, (magic, version, fsize)
    i = hsize
    out = []
    zero = bytes(FRAME)

    def varint():
        nonlocal i
        u = s = 0
        while True:
            c = d[i]; i += 1
            u |= (c & 0x7f) << s; s += 7
            if not c & 0x80: return (u >> 1) ^ -(u & 1)

    def rest(now, before, size):
        nonlocal i
        b = bytearray(before)
        o = 0
        while o < size:
            c = d[i]; i += 1
            if c >= 0x80: o += c - 0x7f; continue
            for _ in range(c + 1): b[o] ^= d[i]; o += 1; i += 1
        return bytes(b)

    for n in range(frames):
        p1 = out[-1] if out else zero
        p2 = out[-2] if len(out) > 1 else zero
        f = bytearray(rest(None, p1[:8], 8))
        for r in range(8):
            a = struct.unpack_from('<3i3h3h', p1, 8 + 52 * r); b = struct.unpack_from('<3i3h3h', p2, 8 + 52 * r)
            pos = [(varint() + 2 * a[k] - b[k] + 2**31) % 2**32 - 2**31 for k in range(3)]
            ang = [(varint() + 2 * a[6 + k] - b[6 + k] + 2**15) % 2**16 - 2**15 for k in range(3)]
            vel = [(varint() + a[3 + k] + 2**15) % 2**16 - 2**15 for k in range(3)] if version > 4 else [0, 0, 0]
            steer = varint(); bits = varint()
            tail = bytearray(rest(None, p1[8 + 52 * r + 24:8 + 52 * r + 52], 28))
            old_steer, = struct.unpack_from('<h', p1, 8 + 52 * r + 32); old_bits, = struct.unpack_from('<H', p1, 8 + 52 * r + 48)
            struct.pack_into('<h', tail, 8, (steer + old_steer + 2**15) % 2**16 - 2**15)
            struct.pack_into('<H', tail, 24, (bits ^ old_bits) & 0xffff)
            f += struct.pack('<3i3h3h', *pos, *vel, *ang) + bytes(tail)
        out.append(bytes(f))
    assert i == hsize + packed, (i, hsize, packed)
    return h, out


def sample(frame, r):
    return struct.unpack_from(SAMPLE, frame, 8 + 52 * r)
