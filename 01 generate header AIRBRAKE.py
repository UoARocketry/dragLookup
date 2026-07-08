from __future__ import annotations

import csv
import argparse
import importlib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(slots=True)
class DragLookupTable:
	"""Parsed representation of one drag lookup CSV file."""

	source_file: Path
	metric: str
	altitude_start_m: float
	altitude_end_m: float
	air_density_kg_m3: float
	deployment_axis: list[float]
	flow_speed_axis: list[float]
	values: list[list[float]]


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


def read_drag_lookup_csv(file_path: str | Path) -> DragLookupTable:
	"""
	Read one drag lookup CSV with this layout:
	1) Altitude row            -> start/end altitude
	2) Air Density row         -> single density value
	3) Header row              -> x-axis deployment percentages
	4..N) Data rows            -> y-axis flow speed + 2D values
	"""

	path = Path(file_path)
	with path.open("r", newline="", encoding="utf-8") as handle:
		rows = list(csv.reader(handle))

	if len(rows) < 4:
		raise ValueError(f"CSV '{path}' does not have the minimum 4 rows.")

	altitude_row = rows[0]
	air_density_row = rows[1]
	table_header_row = rows[2]

	if len(altitude_row) < 3:
		raise ValueError(f"Altitude row in '{path}' must have at least 3 columns.")

	altitude_start_m = _to_float(altitude_row[1], field_name="altitude_start_m")
	altitude_end_m = _to_float(altitude_row[2], field_name="altitude_end_m")

	if len(air_density_row) < 2:
		raise ValueError(f"Air density row in '{path}' must have at least 2 columns.")
	air_density_kg_m3 = _to_float(air_density_row[1], field_name="air_density_kg_m3")

	if len(table_header_row) < 2:
		raise ValueError(f"Table header row in '{path}' must have at least 2 columns.")
	deployment_axis = [
		_to_float(cell, field_name="deployment_axis")
		for cell in table_header_row[1:]
		if cell.strip()
	]

	if not deployment_axis:
		raise ValueError(f"No deployment-axis values found in '{path}'.")

	flow_speed_axis: list[float] = []
	values: list[list[float]] = []

	for row_index, row in enumerate(rows[3:], start=4):
		if not row or not any(cell.strip() for cell in row):
			break

		flow_speed = _to_float(row[0], field_name=f"flow_speed_axis at row {row_index}")
		data_cells = row[1 : 1 + len(deployment_axis)]

		if len(data_cells) < len(deployment_axis):
			raise ValueError(
				f"Row {row_index} in '{path}' has fewer data columns than deployment axis."
			)

		row_values = [
			_to_float(cell, field_name=f"table value at row {row_index}")
			for cell in data_cells
		]

		flow_speed_axis.append(flow_speed)
		values.append(row_values)

	if not flow_speed_axis:
		raise ValueError(f"No 2D table rows found in '{path}'.")

	return DragLookupTable(
		source_file=path,
		metric=_metric_from_filename(path),
		altitude_start_m=altitude_start_m,
		altitude_end_m=altitude_end_m,
		air_density_kg_m3=air_density_kg_m3,
		deployment_axis=deployment_axis,
		flow_speed_axis=flow_speed_axis,
		values=values,
	)


def load_tables_by_metric(csv_files: Iterable[str | Path]) -> dict[str, list[DragLookupTable]]:
	"""Read multiple files and group parsed tables by metric name (e.g. dragCoeff)."""

	grouped: dict[str, list[DragLookupTable]] = {}
	for file_path in csv_files:
		table = read_drag_lookup_csv(file_path)
		grouped.setdefault(table.metric, []).append(table)
	return grouped


def _density_tag(density: float) -> str:
	return f"{int(round(density * 1000.0)):04d}"


def _c_float(value: float) -> str:
	text = f"{value:.9g}"
	if "." not in text and "e" not in text and "E" not in text:
		text += ".0"
	return f"{text}f"


def _flatten_rows(values: list[list[float]]) -> list[float]:
	flat: list[float] = []
	for row in values:
		flat.extend(row)
	return flat


def _join_c_floats(values: list[float], indent: str = "    ", per_line: int = 8) -> str:
	chunks: list[str] = []
	for index in range(0, len(values), per_line):
		row = values[index : index + per_line]
		chunks.append(indent + ", ".join(_c_float(v) for v in row))
	return ",\n".join(chunks)


def _metric_header_name(metric_name: str) -> str:
	return "airbrake_dragCoefficient_lookup.h"


def _render_header(metric_name: str, tables: list[DragLookupTable]) -> str:
	if not tables:
		raise ValueError(f"No tables available for metric '{metric_name}'.")
	sorted_tables = sorted(tables, key=lambda t: t.air_density_kg_m3, reverse=True)
	deployment_count = len(sorted_tables[0].deployment_axis)

	for table in sorted_tables:
		if len(table.deployment_axis) != deployment_count:
			raise ValueError(
				f"Metric '{metric_name}' has inconsistent deployment counts across files."
			)

	upper_metric = metric_name.upper()
	guard = f"AIRBRAKE_{upper_metric}_LOOKUP_H"
	struct_name = "AirbrakeTable"
	value_pointer_name = "cd_values"
	lookup_name = "AIRBRAKE_DRAGCOEFF_TABLE"
	lines: list[str] = []
	lines.append(f"#ifndef {guard}")
	lines.append(f"#define {guard}")
	lines.append("")
	lines.append("// Auto-generated from drag lookup CSV files.")
	lines.append("// Flattened matrix layout: row-major [speed][deployment].")
	lines.append("")
	lines.append("#define AIRBRAKE_DEPLOYMENT_COUNT " + str(deployment_count))
	lines.append(f"#define AIRBRAKE_{upper_metric}_DENSITY_BLOCKS " + str(len(sorted_tables)))
	lines.append("")
	lines.append(f"typedef struct {{")
	lines.append("    float density;")
	lines.append("    int speedCount;")
	lines.append("    const float* speeds;")
	lines.append(f"    const float* {value_pointer_name};")
	lines.append(f"}} {struct_name};")
	lines.append("")

	for table in sorted_tables:
		tag = _density_tag(table.air_density_kg_m3)
		speeds_name = f"SPEEDS_{tag}"
		values_name = f"VALUES_{tag}"
		speeds_joined = _join_c_floats(table.flow_speed_axis)
		flat_values = _flatten_rows(table.values)
		values_joined = _join_c_floats(flat_values)

		lines.append(
			f"static const float {speeds_name}[] = {{\n{speeds_joined}\n}};"
		)
		lines.append(
			f"static const float {values_name}[] = {{\n{values_joined}\n}};"
		)
		lines.append("")

	lines.append(f"static const {struct_name} {lookup_name}[] = {{")
	for table in sorted_tables:
		tag = _density_tag(table.air_density_kg_m3)
		speeds_name = f"SPEEDS_{tag}"
		values_name = f"VALUES_{tag}"
		lines.append(
			"    {"
			+ f"{_c_float(table.air_density_kg_m3)}, "
			+ f"{len(table.flow_speed_axis)}, "
			+ f"{speeds_name}, "
			+ f"{values_name}"
			+ "},"
		)
	lines.append("};")
	lines.append("")
	lines.append(f"#endif  // {guard}")
	lines.append("")

	return "\n".join(lines)


def write_metric_headers(
	tables_by_metric: dict[str, list[DragLookupTable]],
	output_dir: Path,
) -> list[Path]:
	"""Write one .h file per metric and return created file paths."""

	output_dir.mkdir(parents=True, exist_ok=True)
	created: list[Path] = []

	for metric_name, tables in tables_by_metric.items():
		if metric_name != "dragCoeff":
			continue

		header_name = _metric_header_name(metric_name)
		header_path = output_dir / header_name
		header_text = _render_header(metric_name, tables)
		header_path.write_text(header_text, encoding="utf-8")
		created.append(header_path)

	return created


def print_tables_summary(tables_by_metric: dict[str, list[DragLookupTable]]) -> None:
	"""Print one-line metadata summary for each table."""

	for metric_name, tables in tables_by_metric.items():
		print(f"{metric_name}: {len(tables)} files")
		for table in tables:
			print(
				f"  {table.source_file.name}: "
				f"altitude={table.altitude_start_m:.0f}-{table.altitude_end_m:.0f} m, "
				f"density={table.air_density_kg_m3:.3f} kg/m^3, "
				f"table={len(table.flow_speed_axis)}x{len(table.deployment_axis)}"
			)


def show_table_heatmap(table: DragLookupTable) -> None:
	"""Plot a 2D heatmap for one table."""

	try:
		np = importlib.import_module("numpy")
		plt = importlib.import_module("matplotlib.pyplot")
	except ModuleNotFoundError as exc:
		raise RuntimeError(
			"Plotting requires numpy and matplotlib. Install with: pip install numpy matplotlib"
		) from exc

	z_values = np.array(table.values, dtype=float)
	extent = [
		min(table.deployment_axis),
		max(table.deployment_axis),
		min(table.flow_speed_axis),
		max(table.flow_speed_axis),
	]

	plt.figure(figsize=(9, 6))
	image = plt.imshow(
		z_values,
		extent=extent,
		origin="lower",
		aspect="auto",
		interpolation="nearest",
		cmap="viridis",
	)
	plt.colorbar(image, label=table.metric)
	plt.xlabel("Deployment")
	plt.ylabel("Flow Speed")
	plt.title(
		f"{table.metric} | {table.source_file.name} | "
		f"rho={table.air_density_kg_m3:.3f} kg/m^3"
	)
	plt.tight_layout()


def show_table_lines(table: DragLookupTable, max_lines: int = 6) -> None:
	"""Plot metric vs deployment for up to max_lines flow-speed rows."""

	try:
		plt = importlib.import_module("matplotlib.pyplot")
	except ModuleNotFoundError as exc:
		raise RuntimeError(
			"Plotting requires matplotlib. Install with: pip install matplotlib"
		) from exc

	plt.figure(figsize=(9, 6))
	row_count = len(table.flow_speed_axis)
	step = max(1, row_count // max_lines)

	for index in range(0, row_count, step):
		label = f"flow={table.flow_speed_axis[index]:.3f}"
		plt.plot(table.deployment_axis, table.values[index], marker="o", label=label)

	plt.xlabel("Deployment")
	plt.ylabel(table.metric)
	plt.title(f"{table.metric} vs Deployment | {table.source_file.name}")
	plt.grid(alpha=0.25)
	plt.legend()
	plt.tight_layout()


def show_metric_overview(metric_name: str, tables: list[DragLookupTable], mode: str) -> None:
	"""Show all tables in a metric group using the selected plot mode."""

	try:
		plt = importlib.import_module("matplotlib.pyplot")
	except ModuleNotFoundError as exc:
		raise RuntimeError(
			"Plotting requires matplotlib. Install with: pip install matplotlib"
		) from exc

	if not tables:
		return

	for table in sorted(tables, key=lambda t: t.air_density_kg_m3):
		if mode in {"heatmap", "both"}:
			show_table_heatmap(table)
		if mode in {"lines", "both"}:
			show_table_lines(table)

	plt.show()


def _parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="Read and visualize drag lookup CSV tables.")
	parser.add_argument(
		"--pattern",
		default="AIRBRAKE_*.csv",
		help="Glob pattern used inside the csv/ directory.",
	)
	parser.add_argument(
		"--metric",
		default="all",
		help="Metric to plot: dragCoeff or all.",
	)
	parser.add_argument(
		"--plot",
		default="heatmap",
		choices=["heatmap", "lines", "both", "none"],
		help="Plot style to display.",
	)
	parser.add_argument(
		"--write-headers",
		action="store_true",
		help="Generate one .h lookup file per metric.",
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

	tables_by_metric = load_tables_by_metric(csv_dir.glob(args.pattern))
	print_tables_summary(tables_by_metric)

	if args.write_headers:
		output_dir = base_dir / args.headers_dir
		created_files = write_metric_headers(tables_by_metric, output_dir)
		for created_file in created_files:
			print(f"generated: {created_file}")

	if args.plot == "none":
		raise SystemExit(0)

	if args.metric == "all":
		for metric_name, metric_tables in tables_by_metric.items():
			show_metric_overview(metric_name, metric_tables, args.plot)
	else:
		if args.metric == "dragCoeff":
			show_metric_overview(args.metric, tables_by_metric.get(args.metric, []), args.plot)
