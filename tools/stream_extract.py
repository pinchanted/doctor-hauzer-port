#!/usr/bin/env python3
"""Demux a 3DO DataStream movie (OrgData/stream/*DS) into an AVI ffmpeg can decode.

The streams are 32 KB-blocked chunk files: SHDR/FILL/CTRL, FILM (FHDR + FRME = Cinepak frames)
and SNDS (SDX2 audio, 22050 Hz stereo). ffmpeg's 3dostr demuxer reads only the audio, so this
tool writes the video into a 'cvid' AVI on a constant 15 fps timeline (frames are stamped in
240 Hz audio ticks; empty chunks repeat the previous frame), then lets ffmpeg add the audio.

usage: stream_extract.py <stream file> <out.mkv>         (needs ffmpeg on PATH)
       stream_extract.py --avi <stream file> <out.avi>   (video only, no ffmpeg)
"""
import os, struct, subprocess, sys, tempfile

TICKS = 240.0   # 3DO stream clock (audio ticks per second)
FPS = 15   # every stream advances 16 ticks per frame: 15 fps, so no frame is repeated


def frames(d):
    """[(time, width, height, cinepak frame bytes)], FHDR info"""
    out, hdr, i = [], None, 0
    while i + 8 <= len(d):
        tag, size = d[i:i + 4], struct.unpack_from('>I', d, i + 4)[0]
        if size < 8:
            break
        if tag == b'FILM':
            sub = d[i + 16:i + 20]
            if sub == b'FHDR':
                h, w = struct.unpack_from('>II', d, i + 28)
                hdr = (w, h)
            elif sub == b'FRME':
                t = struct.unpack_from('>I', d, i + 8)[0]
                n = struct.unpack_from('>I', d, i + 24)[0]   # frame size
                p = i + 28
                fw, fh = struct.unpack_from('>HH', d, p + 4)
                data = d[p:p + min(n, size - 28)]
                # 3DO / Sega FILM Cinepak: 6 extra bytes (FE 00 00 06 00 00) after the 10-byte frame
                # header; drop them and fix the length so any Cinepak decoder takes the frame
                if data[10:16] == b'\xfe\x00\x00\x06\x00\x00':
                    data = data[:10] + data[16:]
                data = data[:1] + len(data).to_bytes(3, 'big') + data[4:]
                out.append((t, fw, fh, data))
        i += size
    return out, hdr


def runs(fr):
    """split into runs of one frame size (EDDS changes size twice: each run is shown centred)"""
    out = []
    for f in fr:
        if not out or (f[1], f[2]) != (out[-1][0][1], out[-1][0][2]):
            out.append([])
        out[-1].append(f)
    return out


def write_avi(path, fr, w, h, hold=True):
    t0 = fr[0][0]
    slots = {}
    for t, fw, fh, data in fr:
        k = round((t - t0) * FPS / TICKS)
        slots[k] = data
    n = max(slots) + 1 + (round(16 * FPS / TICKS) if hold else 0)   # hold the last frame for its duration
    chunks = [slots.get(k, b'') for k in range(n)]
    big = max(len(c) for c in chunks)

    def ck(tag, data):
        return tag + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')

    def lst(tag, data):
        return b'LIST' + struct.pack('<I', len(data) + 4) + tag + data

    avih = struct.pack('<IIIIIIIIII4I', 1000000 // FPS, big * FPS, 0, 0x10, n, 0, 1, big, w, h, 0, 0, 0, 0)
    strh = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'vids', b'cvid', 0, 0, 0, 0, 1, FPS, 0, n, big, 0xFFFFFFFF, 0, 0, 0, w, h)
    strf = struct.pack('<IiiHH4sIiiII', 40, w, h, 1, 24, b'cvid', w * h * 3, 0, 0, 0, 0)
    hdrl = lst(b'hdrl', ck(b'avih', avih) + lst(b'strl', ck(b'strh', strh) + ck(b'strf', strf)))
    movi, idx, off = b'', b'', 4
    for c in chunks:
        idx += b'00dc' + struct.pack('<III', 0x10 if c else 0, off, len(c))
        e = ck(b'00dc', c)
        movi += e
        off += len(e)
    body = b'AVI ' + hdrl + lst(b'movi', movi) + ck(b'idx1', idx)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', len(body)) + body)
    return n


def main():
    args = sys.argv[1:]
    avi_only = args and args[0] == '--avi'
    if avi_only:
        args = args[1:]
    src, dst = args
    d = open(src, 'rb').read()
    fr, hdr = frames(d)
    if not fr:
        sys.exit('no video frames')
    w = max(f[1] for f in fr)
    h = max(f[2] for f in fr)
    has_audio = b'SNDS' in d
    if avi_only:
        n = write_avi(dst, fr, w, h)
        print(f'{os.path.basename(src)}: {len(fr)} frames {w}x{h} -> {n} @ {FPS} fps')
        return
    with tempfile.TemporaryDirectory() as tmp:
        avi = os.path.join(tmp, 'v.avi')
        n = write_avi(avi, fr, w, h)
        cmd = ['ffmpeg', '-v', 'error', '-y', '-i', avi]
        if has_audio:
            cmd += ['-f', '3dostr', '-i', src, '-map', '0:v', '-map', '1:a', '-c:a', 'flac']
        cmd += ['-c:v', 'ffv1', dst]
        subprocess.run(cmd, check=True)
    print(f'{os.path.basename(src)}: {len(fr)} frames {w}x{h}, {n / FPS:.1f} s, audio {"yes" if has_audio else "no"} -> {dst}')


if __name__ == '__main__':
    main()
