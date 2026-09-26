"""
Downsamples a trimmed (burnout-to-apogee) OpenRocket CSV into a small
C array, averaging velocity/CP/CG/drag_force within fixed-width
velocity buckets. Output is ready to paste directly into your C file
as the `flight_data[]` array.

Usage:
    python3 downsample_flight_data.py "trimmed_flight_data.csv" > flight_data_array.txt
"""

import csv
import sys

STEP = 20  # m/s bucket width — change if you want finer/coarser resolution


def load_rows(path):
    rows = []
    with open(path, newline="") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row or row[0].startswith("#"):
                continue
            try:
                # Same column order as your OpenRocketData struct
                (time, altitude, velocity, aoa, cp, cg,
                 drag_force, cd, pressure, density, mach) = map(float, row)
            except ValueError:
                # Skips header rows / rows with NaN or malformed fields
                continue
            rows.append({
                "velocity": velocity,
                "cp": cp,
                "cg": cg,
                "drag_force": drag_force,
            })
    return rows


def bin_rows(rows, step):
    buckets = {}
    for r in rows:
        bucket = int(r["velocity"] // step) * step
        buckets.setdefault(bucket, []).append(r)

    result = []
    for bucket in sorted(buckets.keys()):
        group = buckets[bucket]
        avg_v = sum(r["velocity"] for r in group) / len(group)
        avg_cp = sum(r["cp"] for r in group) / len(group)
        avg_cg = sum(r["cg"] for r in group) / len(group)
        avg_drag = sum(r["drag_force"] for r in group) / len(group)
        result.append((avg_v, avg_cp, avg_cg, avg_drag, len(group)))
    return result


def emit_c_array(data):
    print("const FlightPoint flight_data[] = {")
    for v, cp, cg, drag, n in data:
        print(f"    {{ {v:.4f}f, {cp:.4f}f, {cg:.4f}f, {drag:.4f}f }},  // n={n}")
    print("};")
    print()
    print("const int flight_data_count = sizeof(flight_data) / sizeof(flight_data[0]);")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 downsample_flight_data.py <trimmed_csv_path>", file=sys.stderr)
        sys.exit(1)

    rows = load_rows(sys.argv[1])
    if not rows:
        print("No valid rows found — check the CSV path/format.", file=sys.stderr)
        sys.exit(1)

    data = bin_rows(rows, STEP)
    emit_c_array(data)