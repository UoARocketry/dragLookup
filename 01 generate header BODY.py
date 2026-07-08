from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path


@dataclass(slots=True)
class BodyLookupSeries:
	source_file: Path
	metric: str
	altitude_sealevel_m: list[float]
	altitude_agl_m: list[float]
	vertical_velocity_m_s: list[float]
	values: list[float]


def _to_float(raw: str, *, field_name: str) -> float:
	text = raw.strip()
	if not text:
		raise ValueError(f"Missing numeric value for {field_name}.")
	return float(text)


def _metric_from_filename(path: Path) -> str:
	name = path.stem
	parts = name.split("_")
	if len(parts) >= 2:
		return parts[1]
	return "unknown"


def _c_float(value: float) -> str:
	text = f"{value:.9g}"
	if "." not in text and "e" not in text and "E" not in text:
		text += ".0"
	return f"{text}f"


def _join_c_floats(values: list[float], indent: str = "    ", per_line: int = 8) -> str:
	chunks: list[str] = []
	for index in range(0, len(values), per_line):
		row = values[index : index + per_line]
		chunks.append(indent + ", ".join(_c_float(v) for v in row))
	return ",\n".join(chunks)


def read_body_lookup_csv(file_path: str | Path) -> BodyLookupSeries:
	path = Path(file_path)

	with path.open("r", newline="", encoding="utf-8") as handle:
		rows = list(csv.reader(handle))

	if len(rows) < 2:
		raise ValueError(f"CSV '{path}' must include header plus at least one data row.")

	header = [cell.strip() for cell in rows[0]]
	if len(header) < 4:
		raise ValueError(f"CSV '{path}' must have 4 columns.")

	altitude_sealevel_m: list[float] = []
	altitude_agl_m: list[float] = []
	vertical_velocity_m_s: list[float] = []
	values: list[float] = []

	for row_index, row in enumerate(rows[1:], start=2):
		if not row or not any(cell.strip() for cell in row):
			break
		if len(row) < 4:
			raise ValueError(f"Row {row_index} in '{path}' has fewer than 4 columns.")

		altitude_sealevel_m.append(
			_to_float(row[0], field_name=f"altitude_sealevel_m row {row_index}")
		)
		altitude_agl_m.append(_to_float(row[1], field_name=f"altitude_agl_m row {row_index}"))
		vertical_velocity_m_s.append(
			_to_float(row[2], field_name=f"vertical_velocity_m_s row {row_index}")
		)
		values.append(_to_float(row[3], field_name=f"value row {row_index}"))

	if not values:
		raise ValueError(f"No data rows found in '{path}'.")

	return BodyLookupSeries(
		source_file=path,
		metric=_metric_from_filename(path),
		altitude_sealevel_m=altitude_sealevel_m,
		altitude_agl_m=altitude_agl_m,
		vertical_velocity_m_s=vertical_velocity_m_s,
		values=values,
	)


def _metric_header_name(metric_name: str) -> str:
	return "body_dragCoefficient_lookup.h"


def _render_header(series: BodyLookupSeries) -> str:
	upper_metric = series.metric.upper()
	guard = f"BODY_{upper_metric}_LOOKUP_H"
	row_count = len(series.values)

	value_name = "BODY_CD"
	table_name = "BODY_DRAGCOEFF_TABLE"
	sealevel_text = _join_c_floats(series.altitude_sealevel_m)
	agl_text = _join_c_floats(series.altitude_agl_m)
	speed_text = _join_c_floats(series.vertical_velocity_m_s)
	value_text = _join_c_floats(series.values)

	lines: list[str] = []
	lines.append(f"#ifndef {guard}")
	lines.append(f"#define {guard}")
	lines.append("")
	lines.append("// Auto-generated from BODY lookup CSV files.")
	lines.append("")
	lines.append(f"#define BODY_{upper_metric}_ROW_COUNT {row_count}")
	lines.append("")
	lines.append("typedef struct {")
	lines.append("    float altitudeSealevelM;")
	lines.append("    float altitudeAglM;")
	lines.append("    float verticalVelocityMps;")
	lines.append(f"    float {value_name};")
	lines.append("} BodyLookupRow;")
	lines.append("")
	lines.append(f"static const float BODY_ALTITUDE_SEALEVEL_M[{row_count}] = {{")
	lines.append(sealevel_text)
	lines.append("};")
	lines.append(f"static const float BODY_ALTITUDE_AGL_M[{row_count}] = {{")
	lines.append(agl_text)
	lines.append("};")
	lines.append(f"static const float BODY_VERTICAL_VELOCITY_MPS[{row_count}] = {{")
	lines.append(speed_text)
	lines.append("};")
	lines.append(f"static const float {value_name}[{row_count}] = {{")
	lines.append(value_text)
	lines.append("};")
	lines.append("")
	lines.append(f"static const BodyLookupRow {table_name}[{row_count}] = {{")
	for index in range(row_count):
		lines.append(
			"    {"
			+ f"{_c_float(series.altitude_sealevel_m[index])}, "
			+ f"{_c_float(series.altitude_agl_m[index])}, "
			+ f"{_c_float(series.vertical_velocity_m_s[index])}, "
			+ f"{_c_float(series.values[index])}"
			+ "},"
		)
	lines.append("};")
	lines.append("")
	lines.append(f"#endif  // {guard}")
	lines.append("")
	return "\n".join(lines)


def write_body_header(series: BodyLookupSeries, output_dir: Path) -> Path:
	output_dir.mkdir(parents=True, exist_ok=True)
	header_path = output_dir / _metric_header_name(series.metric)
	header_path.write_text(_render_header(series), encoding="utf-8")
	return header_path


def _parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="Generate BODY lookup headers from CSV files.")
	parser.add_argument(
		"--pattern",
		default="BODY_*.csv",
		help="Glob pattern used inside the csv/ directory.",
	)
	parser.add_argument(
		"--headers-dir",
		default="generated",
		help="Output directory for generated .h files.",
	)
	return parser.parse_args()


if __name__ == "__main__":
	args = _parse_args()
	base_dir = Path(__file__).resolve().parent
	csv_dir = base_dir / "csv"

	files = sorted(csv_dir.glob(args.pattern))
	if not files:
		raise SystemExit(f"No BODY files found for pattern: {args.pattern}")

	output_dir = base_dir / args.headers_dir
	for file_path in files:
		series = read_body_lookup_csv(file_path)
		if series.metric != "dragCoeff":
			continue
		header_path = write_body_header(series, output_dir)
		print(f"generated: {header_path}")
