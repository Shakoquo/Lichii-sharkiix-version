#!/usr/bin/env python3
"""Builds flash_<frame>.html next to each report of an EDEN_NX2_FLASH_WATCH directory.

Each page lists the outputs of the flash frame in GPU order with their tiles in the two frames
before, the flash frame and the two after. Thumbnails are scaled by the brightest tile of the
frames around the flash, so a tile that flashes saturates; NaN/Inf tiles are magenta and tiles
with hot texels orange. The box is the flashed region.

Usage: python3 -I tools/nx2/flash_report.py <watch dir>
"""
import base64
import html
import json
import struct
import sys
import zlib
from pathlib import Path

WINDOW = 5
CENTER = 2
LABELS = ["N-2", "N-1", "N", "N+1", "N+2"]


def png(width, height, rgb):
    rows = b"".join(b"\x00" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9))
            + chunk(b"IEND", b""))


def tile_means(output, tiles):
    """Mean of the finite texels of each tile, or None when the tile has none."""
    gx, gy = output["grid"]
    width, height = output["width"], output["height"]
    means = []
    for index, (total, _peak, bad, _hot) in enumerate(tiles):
        x, y = index % gx, index // gx
        texels = (((x + 1) * width // gx - x * width // gx)
                  * ((y + 1) * height // gy - y * height // gy)) - bad
        means.append(total / texels if texels > 0 else None)
    return means


def thumbnails(output, blob):
    gx, gy = output["grid"]
    count = gx * gy
    present = [output["region"]["mean"][f] is not None for f in range(WINDOW)]
    frames = []
    for f in range(WINDOW):
        start = (output["tiles"] + f * count) * 16
        frames.append(list(struct.iter_unpack("<ffII", blob[start:start + count * 16])))
    means = [tile_means(output, tiles) for tiles in frames]
    around = [m for f in range(WINDOW) if present[f] and f != CENTER for m in means[f] if m]
    scale = max(around, default=0.0) or max((m for m in means[CENTER] if m), default=0.0) or 1.0
    images = []
    for f in range(WINDOW):
        if not present[f]:
            images.append(None)
            continue
        rgb = bytearray()
        for (_total, _peak, bad, hot), mean in zip(frames[f], means[f]):
            if bad:
                rgb += b"\xff\x00\xff"
            elif hot:
                rgb += b"\xff\x80\x00"
            else:
                level = int(min(1.0, max(0.0, (mean or 0.0) / scale)) ** (1 / 2.2) * 255 + 0.5)
                rgb += bytes((level, level, level))
        images.append(base64.b64encode(png(gx, gy, bytes(rgb))).decode())
    return images


def fmt_values(values):
    return " ".join("-" if v is None else f"{v:.3g}" if isinstance(v, float) else str(v)
                    for v in values)


def describe(output):
    kind = output["type"]
    where = f'{output["format"]} {output["width"]}x{output["height"]} {output["gpu_addr"]}'
    if kind == "pass":
        return f'pass {output["pass"]} rt{output["slot"]} {where}, {output["draws"]} draws'
    if kind == "dispatch":
        return f'dispatch {output["dispatch"]} img{output["slot"]} {where}'
    return f'present {output["present"]} {where}'


def build(report_path):
    lines = [json.loads(line) for line in report_path.read_text().splitlines() if line.strip()]
    flash, outputs = lines[0], lines[1:]
    blob = (report_path.parent / flash["tile_file"]).read_bytes()
    x0, y0, x1, y1 = flash["region"]
    box = (f"left:{x0 * 100:.2f}%;top:{y0 * 100:.2f}%;"
           f"width:{(x1 - x0) * 100:.2f}%;height:{(y1 - y0) * 100:.2f}%")
    first = flash["first_abnormal"]
    rows = []
    for output in outputs:
        images = thumbnails(output, blob)
        cells = "".join(
            f'<td class="thumb"><div><img src="data:image/png;base64,{image}">'
            f'<span style="{box}"></span></div></td>' if image else "<td>-</td>"
            for image in images)
        shaders = " ".join(output["shaders"][:4]) + (
            f' +{len(output["shaders"]) - 4}' if len(output["shaders"]) > 4 else "")
        region = output["region"]
        classes = ["abnormal"] if output["abnormal"] else []
        if output["index"] == first:
            classes.append("first")
        ratio = output["mean_ratio"]
        peak = output["peak_ratio"]
        rows.append(
            f'<tr id="o{output["index"]}" class="{" ".join(classes)}">'
            f'<td>{output["index"]}</td><td>{html.escape(describe(output))}'
            f'<br><code>{html.escape(shaders)}</code></td>'
            f'<td>{"-" if ratio is None else f"{ratio:.3g}"}</td>'
            f'<td>{"-" if peak is None else f"{peak:.3g}"}</td>'
            f'<td class="v">{fmt_values(region["mean"])}</td>'
            f'<td class="v">{fmt_values(region["peak"])}</td>'
            f'<td class="v">{fmt_values(region["bad"])}</td>'
            f'<td class="v">{fmt_values(region["hot"])}</td>'
            f"{cells}</tr>")
    first_link = f'<a href="#o{first}">output {first}</a>' if first is not None else "none"
    page = f"""<!doctype html><meta charset="utf-8"><title>Flash at frame {flash["frame"]}</title>
<style>
body {{ font: 13px sans-serif; background: #111; color: #ddd; }}
table {{ border-collapse: collapse; }}
td, th {{ border: 1px solid #333; padding: 3px 5px; vertical-align: middle; }}
tr.abnormal {{ background: #3a2a10; }}
tr.first {{ background: #6a2020; }}
code {{ color: #9ab; font-size: 11px; }}
.v {{ white-space: nowrap; }}
.thumb div {{ position: relative; width: 160px; }}
.thumb img {{ width: 160px; image-rendering: pixelated; display: block; }}
.thumb span {{ position: absolute; outline: 1px solid #0f0; }}
</style>
<h1>Flash at frame {flash["frame"]} ({flash["seconds"]:.1f} s)</h1>
<p>Frames {", ".join(map(str, flash["frames"]))}; trigger <code>{html.escape(flash["trigger"])}</code>,
{len(flash["tiles"])} tiles brighter, region ({x0:.2f},{y0:.2f})-({x1:.2f},{y1:.2f}).
First abnormal output: {first_link}; {flash["abnormal"]} of {flash["outputs"]} abnormal.
Values per column are frames {", ".join(LABELS)}, measured in the region.</p>
<table><tr><th>#</th><th>output</th><th>mean ratio</th><th>peak ratio</th><th>mean</th>
<th>peak</th><th>NaN/Inf</th><th>hot</th>{"".join(f"<th>{label}</th>" for label in LABELS)}</tr>
{chr(10).join(rows)}
</table>
"""
    out = report_path.with_suffix(".html")
    out.write_text(page, encoding="utf-8")
    culprit = describe(outputs[first]) if first is not None else "none"
    print(f'{out}: frame {flash["frame"]}, first abnormal: {culprit}')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    reports = sorted(Path(sys.argv[1]).glob("flash_*.jsonl"))
    if not reports:
        sys.exit(f"no flash_*.jsonl in {sys.argv[1]}")
    for report in reports:
        build(report)


if __name__ == "__main__":
    main()
