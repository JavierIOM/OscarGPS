"""Exports the plywood box layers from box.scad to a LightBurn-ready SVG.

OpenSCAD writes 2D output as one filled grey shape with a 0.5 mm stroke. This reruns the
export and rewrites the style to a black hairline with no fill, so every contour imports as a
cut. Set OPENSCAD to the binary if it isn't in the default Windows location.
"""

import os
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).parent
OPENSCAD = os.environ.get("OPENSCAD", r"C:\Program Files\OpenSCAD\openscad.exe")
OUT = HERE / "oscargps-box-ply.svg"


def main():
    subprocess.run([OPENSCAD, "-o", str(OUT), "-D", 'part="sheet"', str(HERE / "box.scad")],
                   check=True, capture_output=True)
    svg = OUT.read_text(encoding="utf-8")
    svg, n = re.subn(r'stroke="[^"]*" fill="[^"]*" stroke-width="[^"]*"',
                     'fill="none" stroke="#000000" stroke-width="0.1"', svg)
    assert n == 1, f"expected one styled path, found {n}"
    svg = svg.replace("<title>OpenSCAD Model</title>",
                      "<title>OscarGPS box, 8 layers of 3.6 mm ply</title>")
    OUT.write_text(svg, encoding="utf-8")
    contours = len(re.findall(r"[zZ]", svg.split("<path", 1)[1]))
    size = re.search(r'width="([^"]+)" height="([^"]+)"', svg).groups()
    print(f"{OUT.name}: {contours} contours, sheet {size[0]} x {size[1]}")


if __name__ == "__main__":
    main()
