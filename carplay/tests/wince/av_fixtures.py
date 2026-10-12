import os
"""Generate finite test data independently with FFmpeg, never from phone media."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / '../build/carplay/vehicle-tests'
FFMPEG = Path(os.environ.get('CARPLAY_FFMPEG', 'ffmpeg'))


def pack(codec, width, height, fps, frames):
    data = struct.pack('<6I', 0x31545641, codec, width, height, fps, len(frames))
    return data + b''.join(struct.pack('<II', len(body), int(key)) + body for body, key in frames)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(FFMPEG), '-hide_banner', '-loglevel', 'error', '-y',
                    '-f', 'lavfi', '-i', 'testsrc2=size=800x480:rate=30:duration=6',
                    '-vf', "drawbox=x=0:y=0:w=160:h=120:color=white:t=120:enable='lt(mod(t,1),0.12)',drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='%{n}':x=180:y=20:fontsize=36:fontcolor=white:box=1:boxcolor=black",
                    '-an', '-c:v', 'libx264', '-profile:v', 'baseline', '-level:v', '3.1',
                    '-preset', 'ultrafast', '-tune', 'zerolatency', '-pix_fmt', 'yuv420p',
                    '-x264-params', 'aud=1:repeat-headers=1:keyint=30:min-keyint=30:scenecut=0:bframes=0',
                    '-f', 'h264', str(OUT / 'reference.h264')], check=True)
    raw = (OUT / 'reference.h264').read_bytes()
    units = [n for n in re.split(b'\x00\x00\x00?\x01', raw) if n]
    frames, current, key = [], [], False
    for unit in units:
        kind = unit[0] & 31
        if kind == 9 and current:
            frames.append((b''.join(current), key))
            current, key = [], False
        current.append(b'\x00\x00\x00\x01' + unit)
        key |= kind == 5
    if current:
        frames.append((b''.join(current), key))
    assert len(frames) == 180 and frames[0][1]
    assert sum(key for _, key in frames) == 6
    (OUT / 'video.avt').write_bytes(pack(1, 800, 480, 30, frames))
    # Verify the packed access units can be decoded independently.
    repacked = b''.join(body for body, _ in frames)
    checked = subprocess.run([str(FFMPEG), '-hide_banner', '-loglevel', 'error',
                              '-f', 'h264', '-i', 'pipe:0', '-pix_fmt', 'rgb24',
                              '-f', 'rawvideo', 'pipe:1'], input=repacked, capture_output=True, check=True)
    assert len(checked.stdout) == 180 * 800 * 480 * 3
    # Two seconds of RGB565 bypasses the absent OEM H264 decoder in WM5 emulator.
    rgb = []
    for frame in range(60):
        row = b''.join(struct.pack('<H', 0xf800 if x < (frame*2)%80 else 0x07e0) for x in range(80))
        rgb.append((row * 48, True))
    (OUT / 'rgb.avt').write_bytes(pack(2, 80, 48, 30, rgb))
    subprocess.run([str(FFMPEG), '-hide_banner', '-loglevel', 'error', '-y',
                    '-i', str(OUT / 'reference.h264'), '-frames:v', '1',
                    str(OUT / 'fixture-preview.png')], check=True)
    report = {'h264_frames': 180, 'keyframes': 6, 'width': 800, 'height': 480,
              'fps': 30, 'seconds': 6, 'independently_decoded_frames': 180,
              'flash_seconds': 0.12, 'rgb_emulator_frames': 60,
              'sha256': {n: hashlib.sha256((OUT/n).read_bytes()).hexdigest()
                         for n in ('video.avt', 'rgb.avt', 'reference.h264')}}
    (OUT / 'fixture-validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
