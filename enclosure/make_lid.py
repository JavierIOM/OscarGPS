"""Laser-cut acrylic lid for the OscarGPS case, with glue-on supports and a glue jig.

Writes two SVGs in mm for LightBurn:
  oscargps-lid-acrylic.svg  the lid plus the support clips, 3 mm acrylic
  oscargps-glue-jig.svg     a jig that places the clips, 3 mm card/MDF/ply (not acrylic)

All positions are copied from the printed lid (gps_case_lid2.stl) and are in the base's
coordinates, looking down on the closed box: x to the right, y away from you, origin at the
inside corner of the cavity. The cavity is 74 x 76 mm, the outside 78.4 x 80.4 mm.

Layers (stroke colour): black = cut, blue = screen window (cut it on opaque acrylic, skip it on
clear or smoked), green = score/engrave (jig label only).
"""

from pathlib import Path

OUT = Path(__file__).parent

# Case and lid (from the STLs)
OUTER = (-2.2, -2.2, 76.2, 78.2)              # x0, y0, x1, y1
CORNER_R = 3.0
SCREW_D = 3.4                                 # M3 clearance
SCREWS = [(3.5, 3.5), (70.5, 3.5), (3.5, 72.5), (70.5, 72.5)]
WINDOW = (37.57, 26.57, 70.43, 45.43)         # visible area of the OLED, 32.9 x 18.9
WINDOW_R = 0.5

# Support clips, as polygons. Inner faces match the printed lid; widths grown where there is
# room. Screen pocket: x 36.05..71.95, PCB edge at y 21.0 (header side, kept open in the middle).
# GPS pocket: x 6.0..32.0, y 25.05..50.95, open mid-side on all four sides for cables.
SCREEN_CLIPS = {
    "screen TL": [(34.85, 18.3), (42.05, 18.3), (42.05, 21.0), (36.05, 21.0), (36.05, 27.0), (34.85, 27.0)],
    "screen TR": [(65.95, 18.3), (73.45, 18.3), (73.45, 27.0), (71.95, 27.0), (71.95, 21.0), (65.95, 21.0)],
    "screen BL": [(34.85, 49.0), (36.05, 49.0), (36.05, 57.5), (31.5, 57.5), (31.5, 55.5), (34.85, 55.5)],
    "screen BR": [(71.95, 49.0), (73.45, 49.0), (73.45, 57.5), (71.95, 57.5)],
}
GPS_CLIPS = {
    "gps LL": [(3.5, 22.55), (12.0, 22.55), (12.0, 25.05), (6.0, 25.05), (6.0, 31.05), (3.5, 31.05)],
    "gps LR": [(26.0, 22.55), (33.2, 22.55), (33.2, 31.05), (32.0, 31.05), (32.0, 25.05), (26.0, 25.05)],
    "gps UL": [(3.5, 44.95), (6.0, 44.95), (6.0, 50.95), (12.0, 50.95), (12.0, 53.45), (3.5, 53.45)],
    "gps UR": [(32.0, 44.95), (33.2, 44.95), (33.2, 53.45), (26.0, 53.45), (26.0, 50.95), (32.0, 50.95)],
}
GPS_LAYERS = 2                                # 2 x 3 mm stacked = 6 mm, printed was 6.5
JIG_CLEARANCE = 0.2                           # jig cutouts grow by this on every side
GAP = 3.0                                     # spacing between parts on the sheet
MARGIN = 5.0

CUT, WINDOW_LAYER, SCORE = "#000000", "#0000FF", "#00A000"


def offset_rectilinear(poly, d):
    """Grows an axis-aligned polygon outward by d (works for concave corners too)."""
    area = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]))
    pts = poly if area > 0 else poly[::-1]   # make it counter-clockwise
    n = len(pts)
    lines = []
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        if x0 == x1:   # counter-clockwise, y up: going up, outside is +x
            lines.append(("v", x0 + (d if y1 > y0 else -d)))
        else:          # going right, outside is -y
            lines.append(("h", y0 - (d if x1 > x0 else -d)))
    out = []
    for i in range(n):
        a, b = lines[i - 1], lines[i]
        out.append((a[1], b[1]) if a[0] == "v" else (b[1], a[1]))
    return out


def rounded_rect(x0, y0, x1, y1, r):
    return (f"M {x0 + r:.3f} {y0:.3f} H {x1 - r:.3f} A {r} {r} 0 0 1 {x1:.3f} {y0 + r:.3f} "
            f"V {y1 - r:.3f} A {r} {r} 0 0 1 {x1 - r:.3f} {y1:.3f} H {x0 + r:.3f} "
            f"A {r} {r} 0 0 1 {x0:.3f} {y1 - r:.3f} V {y0 + r:.3f} A {r} {r} 0 0 1 {x0 + r:.3f} {y0:.3f} Z")


class Sheet:
    """Collects paths in sheet coordinates (mm, y down) and writes an SVG."""

    def __init__(self):
        self.items, self.w, self.h = [], 0.0, 0.0

    def add(self, d, colour, extent):
        self.items.append((d, colour))
        self.w, self.h = max(self.w, extent[0]), max(self.h, extent[1])

    def write(self, path, title):
        w, h = self.w + MARGIN, self.h + MARGIN
        body = "\n".join(f'  <path d="{d}" fill="none" stroke="{c}" stroke-width="0.1"/>'
                         for d, c in self.items)
        path.write_text(
            f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.2f}mm" height="{h:.2f}mm" '
            f'viewBox="0 0 {w:.2f} {h:.2f}">\n  <title>{title}</title>\n{body}\n</svg>\n',
            encoding="utf-8")


def placer(ox, oy, mirror=False):
    """Maps base coordinates (y up) to sheet coordinates (y down), lid top-left at ox, oy."""
    x0, y0, x1, y1 = OUTER

    def f(x, y):
        sx = (x1 - x) if mirror else (x - x0)
        return ox + sx, oy + (y1 - y)
    return f


def poly_path(poly, f):
    pts = [f(x, y) for x, y in poly]
    return "M " + " L ".join(f"{x:.3f} {y:.3f}" for x, y in pts) + " Z"


def circle_path(cx, cy, r):
    return f"M {cx - r:.3f} {cy:.3f} A {r} {r} 0 1 0 {cx + r:.3f} {cy:.3f} A {r} {r} 0 1 0 {cx - r:.3f} {cy:.3f} Z"


def plate(sheet, f, mirror_extent):
    """Lid-sized plate: outline and screw holes, placed with f."""
    x0, y0, x1, y1 = OUTER
    a, b = f(x0, y1), f(x1, y0)
    left, right = min(a[0], b[0]), max(a[0], b[0])
    sheet.add(rounded_rect(left, a[1], right, b[1], CORNER_R), CUT, (right, b[1]))
    for sx, sy in SCREWS:
        cx, cy = f(sx, sy)
        sheet.add(circle_path(cx, cy, SCREW_D / 2), CUT, (right, b[1]))
    return right, b[1]


def window_path(f):
    wx0, wy0, wx1, wy1 = WINDOW
    a, b = f(wx0, wy1), f(wx1, wy0)
    return rounded_rect(min(a[0], b[0]), a[1], max(a[0], b[0]), b[1], WINDOW_R)


def poly_area(poly):
    return abs(sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]))) / 2


def self_check():
    # Growing must add area for every clip, in either winding: catches a flipped normal
    sq = [(0, 0), (10, 0), (10, 10), (0, 10)]
    assert abs(poly_area(offset_rectilinear(sq, 1)) - 144) < 1e-9
    assert abs(poly_area(offset_rectilinear(sq[::-1], 1)) - 144) < 1e-9
    for name, poly in {**SCREEN_CLIPS, **GPS_CLIPS}.items():
        grown = offset_rectilinear(poly, JIG_CLEARANCE)
        assert poly_area(grown) > poly_area(poly), name
    # Clips stay inside the cavity with room to spare, and clear of each other
    for name, poly in {**SCREEN_CLIPS, **GPS_CLIPS}.items():
        xs, ys = [p[0] for p in poly], [p[1] for p in poly]
        assert min(xs) >= 0.5 and max(xs) <= 73.5 and min(ys) >= 0.5 and max(ys) <= 75.5, name


def main():
    self_check()
    lid_w, lid_h = OUTER[2] - OUTER[0], OUTER[3] - OUTER[1]

    # Acrylic sheet: lid, then the clips laid out in a column beside it
    acrylic = Sheet()
    f = placer(MARGIN, MARGIN)
    right, bottom = plate(acrylic, f, False)
    acrylic.add(window_path(f), WINDOW_LAYER, (right, bottom))

    pieces = list(SCREEN_CLIPS.values()) + [p for p in GPS_CLIPS.values() for _ in range(GPS_LAYERS)]
    col_x, cur_x, cur_y, row_h = right + GAP * 2, right + GAP * 2, MARGIN, 0.0
    max_x = col_x + 50
    for poly in pieces:
        xs, ys = [p[0] for p in poly], [p[1] for p in poly]
        pw, ph = max(xs) - min(xs), max(ys) - min(ys)
        if cur_x + pw > max_x:
            cur_x, cur_y, row_h = col_x, cur_y + row_h + GAP, 0.0
        moved = [(x - min(xs) + cur_x, max(ys) - y + cur_y) for x, y in poly]
        acrylic.add("M " + " L ".join(f"{x:.3f} {y:.3f}" for x, y in moved) + " Z", CUT,
                    (cur_x + pw, cur_y + ph))
        cur_x += pw + GAP
        row_h = max(row_h, ph)
    acrylic.write(OUT / "oscargps-lid-acrylic.svg", "OscarGPS acrylic lid and supports")

    # Glue jig: the underside view (mirrored left to right), so it goes on face up over the
    # lid lying face down. Cutouts are the clips grown by the clearance; the window hole is a
    # sight check that the jig isn't flipped or turned round.
    jig = Sheet()
    g = placer(MARGIN, MARGIN, mirror=True)
    right, bottom = plate(jig, g, True)
    for poly in SCREEN_CLIPS.values():
        jig.add(poly_path(offset_rectilinear(poly, JIG_CLEARANCE), g), CUT, (right, bottom))
    for poly in GPS_CLIPS.values():
        jig.add(poly_path(offset_rectilinear(poly, JIG_CLEARANCE), g), CUT, (right, bottom))
    jig.add(window_path(g), CUT, (right, bottom))
    # "UP" arrow scored in a clear spot of the jig, pointing away from you (base +y)
    ax, ay = g(18.0, 66.0)
    arrow = [(ax, ay - 6), (ax + 4, ay - 1), (ax + 1.5, ay - 1), (ax + 1.5, ay + 5),
             (ax - 1.5, ay + 5), (ax - 1.5, ay - 1), (ax - 4, ay - 1)]
    jig.add("M " + " L ".join(f"{x:.3f} {y:.3f}" for x, y in arrow) + " Z", SCORE, (right, bottom))
    jig.write(OUT / "oscargps-glue-jig.svg", "OscarGPS glue jig, cut from card or MDF")

    print(f"lid {lid_w:.1f} x {lid_h:.1f} mm, {len(pieces)} clips; sheet {acrylic.w + MARGIN:.0f} x "
          f"{acrylic.h + MARGIN:.0f} mm; jig {jig.w + MARGIN:.0f} x {jig.h + MARGIN:.0f} mm")


if __name__ == "__main__":
    main()
