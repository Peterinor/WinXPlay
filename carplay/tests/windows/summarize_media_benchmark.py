"""Summarize repeatable local media benchmarks; never infer display scanout FPS."""
import json
from pathlib import Path
import statistics
import sys

source, output = map(Path, sys.argv[1:3])
rows = []
for mode in ('paced', 'unpaced'):
    for width in (1280, 1920):
        for backend in ('system', 'ffmpeg'):
            files = sorted(source.glob(f'benchmark-{mode}-{width}-{backend}-*.json'))
            assert len(files) == 3, (mode, width, backend, len(files))
            records = [json.loads(p.read_text()) for p in files]
            assert all(r['decoded_frames'] == 180 and r['hresult'] == 0 for r in records)
            fps = [r['processed_fps'] for r in records]
            rows.append(dict(mode=mode, width=width, backend=backend, rounds=len(records),
                             processed_fps_median=statistics.median(fps), processed_fps_min=min(fps),
                             processed_fps_max=max(fps),
                             display_submissions=[r['display_submissions'] for r in records],
                             cpu_ms_median=statistics.median(r['total_cpu_ms'] for r in records),
                             cpu_one_core_percent_median=statistics.median(
                                 r['total_cpu_ms'] / r['elapsed_ms'] * 100 for r in records),
                             raw_files=[p.name for p in files]))
result = dict(rows=rows, video='testsrc2, H.264, 180 frames, no B frames, 30fps source',
              render_client='1280x720, DPI aware, same client for both input resolutions',
              ffmpeg='existing LiveVideo pipeline, D3D11VA requested, BGR pipe + GDI',
              system='Media Foundation H.264, D3D11 decoder surfaces + GPU video processor + Flip',
              cpu_includes_ffmpeg_child=True, physical_display_fps_measured=False,
              end_to_end_latency_measured=False,
              limitations=['Short synthetic clip; not a phone/network/AV-sync benchmark',
                           'Unpaced decoded FPS includes initialization and drain',
                           'GDI may coalesce repaints; DXGI/DWM may discard submitted frames',
                           'CPU values are consumed CPU time, not whole-machine utilization'])
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(rows, indent=2))
