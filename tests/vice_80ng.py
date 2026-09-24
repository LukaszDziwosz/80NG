#!/usr/bin/env python3
"""Exercise the actual Oscar64 binary and VDC RAM, using VICE's binary monitor.
Run from the project root: python3 tests/vice_80ng.py [--ntsc] [--ram 64]
The dedicated emulator is closed after the test. No third-party Python modules.
"""
import argparse
import socket
import struct
import subprocess
import time
import zlib
from pathlib import Path

# Screenshots and VICE console logs are local test artifacts, not part of the
# repository. Everything under build/ except 80ng.prg is git-ignored.
ARTIFACTS = Path('build/artifacts')


class Monitor:
    def __init__(self, port):
        self.s = socket.create_connection(('127.0.0.1', port), 5)
        self.s.settimeout(10)
        self.rid = 0

    def read(self, n):
        data = bytearray()
        while len(data) < n:
            part = self.s.recv(n - len(data))
            if not part:
                raise EOFError('VICE closed the monitor')
            data.extend(part)
        return bytes(data)

    def response(self):
        _, _, size, kind, error, rid = struct.unpack('<BBIBBI', self.read(12))
        return kind, error, rid, self.read(size)

    def cmd(self, command, body=b''):
        self.rid += 1
        self.s.sendall(struct.pack('<BBIIB', 2, 2, len(body), self.rid, command) + body)
        while True:
            kind, error, rid, data = self.response()
            if rid == self.rid:
                if error:
                    raise RuntimeError(f'VICE command {command:02x}: error {error:02x}')
                return data

    def memory(self, addr, size, bank=0):
        return self.cmd(1, struct.pack('<BHHBH', 0, addr, addr+size-1, 0, bank))[2:]

    def write(self, addr, data, bank=0):
        self.cmd(2, struct.pack('<BHHBH', 0, addr, addr+len(data)-1, 0, bank)+data)

    def checkpoint(self, addr):
        return struct.unpack_from('<I', self.cmd(0x12, struct.pack('<HHBBBB', addr, addr, 1, 1, 4, 0)))[0]

    def vdc_register(self, reg):
        self.write(0xd600, bytes([reg]), 3)
        return self.cmd(1, struct.pack('<BHHBH', 1, 0xd601, 0xd601, 0, 3))[2]

    def next_frame(self):
        self.cmd(0xaa)
        while True:
            kind, error, rid, data = self.response()
            if kind == 0x62:
                return

    def capture(self, filename):
        data = self.cmd(0x84, b'\0\0')
        info_len = struct.unpack_from('<I', data)[0]
        w, h = struct.unpack_from('<HH', data, 12)
        offset = 4 + info_len
        size = struct.unpack_from('<I', data, offset)[0]
        pixels = data[offset+4:offset+4+size]
        if len(pixels) == size-4 and not any(pixels[-w:]):
            pixels += bytes(4)  # VICE 3.10 omits four final border pixels.
        assert len(pixels) == w*h
        pal = self.cmd(0x91, b'\0')
        palette, pos = bytearray(), 2
        for _ in range(struct.unpack_from('<H', pal)[0]):
            length = pal[pos]
            palette.extend(pal[pos+1:pos+4])
            pos += length+1
        def chunk(tag, contents):
            return struct.pack('>I', len(contents))+tag+contents+struct.pack('>I', zlib.crc32(tag+contents))
        raw = b''.join(b'\0'+pixels[y*w:(y+1)*w] for y in range(h))
        Path(filename).write_bytes(b'\x89PNG\r\n\x1a\n'+
            chunk(b'IHDR', struct.pack('>IIBBBBB', w,h,8,3,0,0,0))+
            chunk(b'PLTE', palette)+chunk(b'IDAT', zlib.compress(raw))+chunk(b'IEND', b''))


def check_game(m, syms, artifacts):
    def value(name, size=2):
        return int.from_bytes(m.memory(syms[name], size), 'little')

    def state():
        return (struct.unpack('<hhbb', m.memory(syms['ball'], 6)),
                struct.unpack('<hB', m.memory(syms['left'], 3)),
                struct.unpack('<hB', m.memory(syms['right'], 3)))

    def pixels():
        cells = m.memory(0, 2000, 10)  # C128 VDC bank
        assert cells == m.memory(syms['screen'], 2000), 'VDC and CPU screen diverged'
        font = m.memory(0x2000, 47*16, 10)
        actual = bytes(font[cells[(y//8)*80+x]*16 + y%8]
                       for y in range(8, 200) for x in range(80))
        expected = bytearray(192*80)
        # Top and bottom court lines (glyph 1) draw a single pixel row at the
        # top of their character cell: pixel rows 8 (char row 1) and 192
        # (char row 24), column 0..79.
        expected[0:1*80] = b'\xff' * 80
        expected[184*80:185*80] = b'\xff' * 80
        # Centre net (glyph 3 -> 0x18) on even character rows inside the
        # court (rows 2..22), column 40. The glyph fills all 8 pixel rows.
        for row in range(2, 24, 2):
            start = (row*8-8)*80
            for line in range(8):
                expected[start+line*80+40] = 0x18
        b, left, right = state()
        def rectangle(x, y, w, h):
            for py in range(y, y+h):
                for px in range(x, x+w):
                    expected[(py-8)*80+px//8] |= 0x80 >> (px%8)
        rectangle(24, left[0], 8, 32)
        rectangle(608, right[0], 8, 32)
        # Aspect-corrected ball: 4 rows tall, 8 px wide, with the top and
        # bottom rows inset by 2 px so it reads as a disc (pixels are ~2:1).
        ball_rows = ((4, 2), (8, 0), (8, 0), (4, 2))
        for py, (width, inset) in enumerate(ball_rows):
            for px in range(b[0] + inset, b[0] + inset + width):
                expected[(b[1] + py - 8)*80+px//8] |= 0x80 >> (px % 8)
        if actual != expected:
            i = next(i for i, (a,b) in enumerate(zip(actual, expected)) if a != b)
            raise AssertionError(f'Pixel mismatch xbyte={i%80}, y={i//80+8}: {actual[i]:02x} != {expected[i]:02x}; {state()}')
        assert value('blank_overruns') == 0, 'VDC blank overrun'
        assert value('max_dirty', 1) <= 40, 'Dirty queue bound exceeded'

    key_addr = syms['keys']
    checkpoint = m.checkpoint(key_addr)
    m.next_frame()
    original = m.memory(key_addr, 3)
    pixels()
    original_font = m.memory(syms['saved_font'], 47*16)
    saved_regs = m.memory(syms['saved_regs'], 37)

    def held(mask):
        m.write(key_addr, bytes([0xa9, mask, 0x60]))

    def step(mask=0):
        held(mask)
        before = value('frame_count')
        m.next_frame()
        assert (value('frame_count') - before) & 65535 == 1
        pixels()

    def setup(ball, left=(72, 0), right=(72, 0)):
        # Start the next simulated frame with a clean background, so injected
        # teleports do not leave the previous scenario's ball on the screen.
        screen = bytearray(2000)
        # Top and bottom court lines (glyph 1) on character rows 1 and 24.
        screen[1*80:2*80] = b'\x01'*80
        screen[24*80:25*80] = b'\x01'*80
        # Centre net (glyph 3) on even character rows inside the court.
        for y in range(2, 24, 2):
            screen[y*80+40] = 3
        # Score digits start at 0/0 -> glyph 21 (BALL_GLYPH-era DIGITS+0) at
        # the four fixed score cells on the border row.
        screen[27] = screen[28] = screen[49] = screen[50] = 21
        m.write(syms['screen'], screen)
        m.write(0, screen, 10)
        m.write(syms['ball'], struct.pack('<hhbb', *ball))
        m.write(syms['left'], struct.pack('<hB', *left))
        m.write(syms['right'], struct.pack('<hB', *right))

    # All 64 sub-character ball positions, including net overlaps; all bar
    # phases, including odd phases that held movement alone would not reach.
    for yphase in range(8):
        for xphase in range(8):
            setup((317+xphase, 79+yphase, 3, 1), (72+yphase, 0), (81-yphase, 0))
            step()
    print('PASS: all 64 ball phases, bar phases, net compositing, VDC pixels')

    setup((200, 100, 3, 1))
    step(1 | 8)
    assert state()[1][0] == 70 and state()[2][0] == 74
    for _ in range(80):
        step(1 | 8)
    assert state()[1][0] == 8 and state()[2][0] == 160
    for _ in range(80):
        step(2 | 4)
    assert state()[1][0] == 160 and state()[2][0] == 8
    before = state()[1:]
    step(15)
    assert state()[1:] == before
    print('PASS: simultaneous held input, both limits, opposing keys, no trails')

    for ball, expected in [
        # Ball is BALL_W=8 px wide, so its trailing/leading edge must cross
        # the bar face during the step for a hit to register.
        ((35, 90, -3, 1), (32, 91, 3, 1)),     # left face (LEFT_X+8 = 32)
        ((599, 90, 3, 1), (598, 91, -3, 1)),   # right face (RIGHT_X = 608)
        ((200, 8, 3, -1), (203, 9, 3, 1)),     # top wall bounce
        ((200, 188, 3, 1), (203, 187, 3, -1)), # bottom wall bounce
        ((30, 90, -3, 1), (27, 91, -3, 1)),    # no hit: ball stays right of face
    ]:
        setup(ball)
        step()
        assert state()[0] == expected, state()
    setup((9, 130, -3, 1), right=(72, 99))
    step()
    assert state()[0] == (318, 98, 3, -1) and state()[2][1] == 0
    setup((627, 130, 3, 1), left=(72, 9))
    step()
    assert state()[0] == (318, 98, -3, -1) and state()[1][1] == 10
    assert m.memory(27, 2, 10) == bytes([22, 21])
    print('PASS: bar/wall collisions, misses, serving, two-digit points/wrap')

    # Rally speed ladder: base 3, +1 at >=4, >=8, >=16, >=24 and >=32 hits,
    # topping out at BALL_DX_MAX=8 (turbo).
    # rally_hits is bumped by the hit itself, so seed one below the boundary.
    for seed, expected_dx in [
        (0, 3),    # under 4 -> base
        (3, 4),    # 4 after the bump -> +1
        (7, 5),    # 8 -> +2
        (15, 6),   # 16 -> +3
        (23, 7),   # 24 -> +4
        (31, 8),   # 32 -> +5, turbo top speed (BALL_DX_MAX)
        (60, 8),   # well past the top step, still clamped at 8
    ]:
        m.write(syms['rally_hits'], bytes([seed]))
        # Leading edge crosses the left bar face (32) this step with dx<0,
        # so the hit is guaranteed and the outgoing speed is the ladder value.
        # (x=35 with dx=-3 lands exactly on the face and reflects to x=32.)
        setup((35, 90, -3, 1))
        step()
        got = state()[0]
        assert got[0] >= 32 and got[2] == expected_dx, (seed, got)
    print('PASS: rally speed ladder 3->4->5->6->7->8 (turbo), capped')

    m.cmd(0x13, struct.pack('<I', checkpoint))
    held(0)
    before = value('frame_count')
    start = time.monotonic()
    m.cmd(0xaa)
    time.sleep(3)
    after = value('frame_count')
    elapsed = time.monotonic() - start
    fps = ((after-before) & 65535)/elapsed
    assert 55 < fps < 65, fps
    assert value('blank_overruns') == 0
    print(f'Cadence: {fps:.2f} updates/s; blank overruns: 0; max queued cells: {value("max_dirty", 1)}')
    m.capture(str(artifacts / f'80ng-{args.tag}.png'))

    # Exit through the real game path, checking the overwritten font bytes and
    # VDC display/font base registers before leaving a normal KERNAL screen.
    held(16)
    m.cmd(0xaa)
    time.sleep(0.25)
    final = value('frame_count')
    m.cmd(0xaa)
    time.sleep(0.15)
    assert value('frame_count') == final
    assert m.memory(0x2000, 47*16, 10) == original_font
    for reg in list(range(10)) + [12, 13, 20, 21, 22, 23, 24, 25, 27, 28, 34]:
        assert m.vdc_register(reg) == saved_regs[reg], f'VDC register {reg} was not restored'
    assert not (m.memory(0xd030, 1, 3)[0] & 1), 'CPU left in fast mode'
    m.write(key_addr, original)
    m.capture(str(artifacts / f'80ng-{args.tag}-exit.png'))
    print('PASS: quit restores font, display/timing registers, and CPU speed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--ntsc', action='store_true')
    parser.add_argument('--ram', choices=(16, 64), type=int, default=16)
    parser.add_argument('--port', type=int, default=6513)
    args = parser.parse_args()
    args.tag = f'{"ntsc" if args.ntsc else "pal"}-{args.ram}k'
    syms = {line.split()[2].lstrip('.'): int(line.split()[1],16)
            for line in Path('build/80ng.lbl').read_text().splitlines()}
    artifacts = ARTIFACTS
    artifacts.mkdir(parents=True, exist_ok=True)
    logpath = artifacts / f'vice-{args.tag}.log'
    with logpath.open('w') as log:
        p = subprocess.Popen(['x128', '-console', '-default', '-80col',
            '-ntsc' if args.ntsc else '-pal', f'-VDC{args.ram}KB',
            '-sounddev', 'dummy', '-binarymonitor', '-binarymonitoraddress',
            f'127.0.0.1:{args.port}', '-autostartprgmode', '1',
            '-autostart', 'build/80ng.prg'], stdout=log, stderr=log)
        m = None
        try:
            deadline = time.monotonic()+20
            while m is None:
                if p.poll() is not None:
                    raise RuntimeError(logpath.read_text())
                assert time.monotonic() < deadline, 'Monitor startup timed out'
                try:
                    m = Monitor(args.port)
                except ConnectionRefusedError:
                    time.sleep(0.1)
            check_game(m, syms, artifacts)
        finally:
            if m:
                try:
                    m.cmd(0xbb)
                except (EOFError, OSError):
                    pass
                m.s.close()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.terminate()
                p.wait(timeout=3)
