"""Stage01: water footprint (pond + stream) as axis-aligned boxes for invisible pawn blockers. Pure Python.
Water edge from s01r_layout: pond water reaches ellipse_r ~1.0, stream water ~170 cm from the centreline.
The blockers stay slightly inside the waterline; the strip under the wooden bridge is left open."""
import math
import s01r_layout as L

ROW = 60.0        # row height along Y (cm)
COL = 30.0        # sampling step along X (cm)
POND_ER = 0.97    # ellipse radius of the pond blocker
STREAM_HALF = 150.0
STREAM_Y_MIN = -3250.0   # beyond this the stream is the cliff/waterfall (already unwalkable)
BRIDGE_GAP = L.BRIDGE_W / 2 + 40   # keep blockers away from the deck footprint in Y


def is_water(x, y):
    if L.ellipse_r((x, y), L.POND) <= POND_ER:
        return 'Pond'
    if y > STREAM_Y_MIN and L.poly_dist((x, y), L.STREAM) <= STREAM_HALF:
        if abs(y - L.BRIDGE[1]) < BRIDGE_GAP and abs(x - L.BRIDGE[0]) < L.BRIDGE_LEN / 2:
            return None
        return 'Stream'
    return None


def water_z(kind):
    return L.POND_WATER_Z if kind == 'Pond' else L.STREAM_WATER_Z


def boxes():
    """[(label, cx, cy, sx, sy, water_z)] merged runs per row."""
    out = []
    x0, x1, y0, y1 = -3500.0, -1300.0, -3300.0, 100.0
    y = y0
    row = 0
    while y < y1:
        cy = y + ROW / 2
        run = None
        x = x0
        while x <= x1 + COL:
            k = is_water(x, cy) if x <= x1 else None
            if run and k != run[0]:
                xa, xb, kind = run[1], x - COL, run[0]
                out.append(('S01_WaterBlock_%s_%03d_%02d' % (kind, row, len(out) % 100), (xa + xb) / 2, cy,
                            (xb - xa) + COL, ROW + 2, water_z(kind)))
                run = None
            if k and not run:
                run = (k, x)
            x += COL
        y += ROW
        row += 1
    return out


if __name__ == '__main__':
    b = boxes()
    print(len(b), sum(1 for x in b if 'Pond' in x[0]), sum(1 for x in b if 'Stream' in x[0]))
    for t in b[:5]:
        print(t)
