"""
Plots the total drag coefficient over a simulated flight path, showing the
impact of different airbrake deployment levels.

This script reads the generated C header files to get the flight path data
and lookup tables, replicates the C lookup logic in Python, and uses
matplotlib to generate the plots.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import cast

import matplotlib.pyplot as plt
import numpy as np


@dataclass(slots=True)
class AirbrakeTable:
    """Python representation of the C AirbrakeTable struct."""

    density: float
    speed_count: int
    speeds: list[float]
    cd_values: list[float]


def _parse_c_float_array(content: str, name: str) -> list[float]:
    """Extracts a float array from a C header file."""
    match = re.search(
        rf"static const float {name}\[.*?\] = \{{(.*?)\}};",
        content,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"Could not find C array '{name}'")
    # Remove comments, newlines, and trailing commas, then split
    values_str = re.sub(r"//.*", "", match.group(1))
    values_str = values_str.replace("\n", "").strip()
    if values_str.endswith(","):
        values_str = values_str[:-1]
    return [float(v.strip().removesuffix("f")) for v in values_str.split(",") if v.strip()]


def _parse_airbrake_tables(content: str) -> list[AirbrakeTable]:
    """Parses the array of AirbrakeTable structs from the header."""
    table_content_match = re.search(
        r"static const AirbrakeTable AIRBRAKE_DRAGCOEFF_TABLE\[.*?\] = \{(.*?)\};",
        content,
        re.DOTALL,
    )
    if not table_content_match:
        raise ValueError("Could not find AIRBRAKE_DRAGCOEFF_TABLE")

    tables = []
    # Regex to find each struct entry: {density, speedCount, speeds_ptr, values_ptr}
    struct_pattern = re.compile(
        r"\{\s*([\d.eE+-]+f?),\s*(\d+),\s*(\w+),\s*(\w+)\s*\}", re.DOTALL
    )
    for match in struct_pattern.finditer(table_content_match.group(1)):
        density, speed_count_str, speeds_name, values_name = match.groups()
        tables.append(
            AirbrakeTable(
                density=float(density.removesuffix("f")),
                speed_count=int(speed_count_str),
                speeds=_parse_c_float_array(content, speeds_name),
                cd_values=_parse_c_float_array(content, values_name),
            )
        )
    return tables


# --- C Logic Ported to Python ---


def clampf(x: float, min_value: float, max_value: float) -> float:
    return max(min_value, min(x, max_value))


def lerpf(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def airbrake_normalize_deployment(deployment_percent: float) -> float:
    return deployment_percent * 0.01 if deployment_percent > 1.0 else deployment_percent


def altitude_to_block_index(altitude_m: float) -> int:
    if not (290.0 <= altitude_m <= 515.0):
        return -1
    if altitude_m < 340.0:
        return 0
    if altitude_m < 390.0:
        return 1
    if altitude_m < 440.0:
        return 2
    if altitude_m < 490.0:
        return 3
    return 4


def find_bracket_ascending(axis: list[float], x: float) -> tuple[int, int, float]:
    count = len(axis)
    if count <= 1 or x <= axis[0]:
        return 0, 0, 0.0
    if x >= axis[count - 1]:
        return count - 1, count - 1, 0.0

    for i in range(count - 1):
        if axis[i] <= x <= axis[i + 1]:
            span = axis[i + 1] - axis[i]
            t = (x - axis[i]) / span if span != 0.0 else 0.0
            return i, i + 1, t

    return count - 1, count - 1, 0.0


def find_deployment_bracket(deployment_percent: float, deployment_count: int) -> tuple[int, int, float]:
    deployment_min, deployment_max = 0.1, 1.0
    deployment = airbrake_normalize_deployment(deployment_percent)
    deployment = clampf(deployment, deployment_min, deployment_max)

    if deployment_count <= 1:
        return 0, 0, 0.0

    step = (deployment_max - deployment_min) / (deployment_count - 1)
    position = (deployment - deployment_min) / step
    lower = int(position)

    if lower >= deployment_count - 1:
        return deployment_count - 1, deployment_count - 1, 0.0

    return lower, lower + 1, position - lower


def sample_drag_coeff_block(block: AirbrakeTable, speed: float, deployment_percent: float) -> float:
    clamped_speed = clampf(speed, block.speeds[0], block.speeds[-1])
    i0, i1, ts = find_bracket_ascending(block.speeds, clamped_speed)

    deployment_count = 10  # From AIRBRAKE_DEPLOYMENT_COUNT
    j0, j1, td = find_deployment_bracket(deployment_percent, deployment_count)

    width = deployment_count
    v00 = block.cd_values[i0 * width + j0]
    v01 = block.cd_values[i0 * width + j1]
    v10 = block.cd_values[i1 * width + j0]
    v11 = block.cd_values[i1 * width + j1]

    low_speed = lerpf(v00, v01, td)
    high_speed = lerpf(v10, v11, td)
    return lerpf(low_speed, high_speed, ts)


def airbrake_lookup(
    altitude_m: float, speed: float, deployment_percent: float, tables: list[AirbrakeTable]
) -> float:
    normalized_deployment = airbrake_normalize_deployment(deployment_percent)
    if not (0.0 <= normalized_deployment <= 1.0):
        return -1.0

    block_index = altitude_to_block_index(altitude_m)
    if block_index == -1 or block_index >= len(tables):
        return -1.0

    return sample_drag_coeff_block(tables[block_index], speed, deployment_percent)


def interpolate_monotonic_axis(axis: list[float], values: list[float], x: float) -> float:
    count = len(axis)
    if count == 0:
        return -1.0
    if count == 1:
        return values[0]

    first, last = axis[0], axis[-1]
    ascending = last >= first

    clamped_x = clampf(x, first, last) if ascending else clampf(x, last, first)

    for i in range(count - 1):
        a0, a1 = axis[i], axis[i + 1]
        in_segment = a0 <= clamped_x <= a1 if ascending else a1 <= clamped_x <= a0
        if in_segment:
            span = a1 - a0
            t = (clamped_x - a0) / span if span != 0.0 else 0.0
            return lerpf(values[i], values[i + 1], t)

    return values[-1]


def body_lookup(input_value: float, axis: list[float], values: list[float]) -> float:
    return interpolate_monotonic_axis(axis, values, input_value)


def main():
    """Main function to generate and show plots."""
    base_dir = Path(__file__).resolve().parent
    generated_dir = base_dir / "generated"

    # --- Load Data from Headers ---
    try:
        body_header_path = generated_dir / "body_dragCoefficient_lookup.h"
        airbrake_header_path = generated_dir / "airbrake_dragCoefficient_lookup.h"

        body_content = body_header_path.read_text()
        airbrake_content = airbrake_header_path.read_text()
    except FileNotFoundError as e:
        print(f"Error: Could not find generated header file: {e.filename}")
        print("Please run the '01 generate header' scripts first.")
        return

    # Body flight path data
    body_altitudes = _parse_c_float_array(body_content, "BODY_ALTITUDE_SEALEVEL_M")
    body_velocities = _parse_c_float_array(body_content, "BODY_VERTICAL_VELOCITY_MPS")
    body_cds_baseline = _parse_c_float_array(body_content, "BODY_CD")
    timesteps = np.arange(len(body_altitudes))

    # Airbrake lookup tables
    airbrake_tables = _parse_airbrake_tables(airbrake_content)

    # --- Plot 1: Total Drag Coefficient vs. Timestep ---
    print("Generating Plot 1: Total Drag Coefficient vs. Timestep...")
    plt.figure(figsize=(14, 8))

    # Plot baseline (body only)
    plt.plot(timesteps, body_cds_baseline, label="Baseline (0% Deployment)", color="black", linestyle="--")

    # Plot for each deployment level
    deployment_levels = range(10, 101, 10)
    colors = plt.cm.viridis(np.linspace(0, 1, len(deployment_levels)))

    for i, dep_level in enumerate(deployment_levels):
        total_cds = []
        valid_timesteps = []
        for t in timesteps:
            altitude = body_altitudes[t]
            velocity = body_velocities[t]
            body_cd = body_cds_baseline[t]

            airbrake_cd = airbrake_lookup(altitude, velocity, dep_level, airbrake_tables)

            if airbrake_cd != -1.0:
                total_cd = (airbrake_cd * 3.0) + body_cd
                total_cds.append(total_cd)
                valid_timesteps.append(t)

        plt.plot(valid_timesteps, total_cds, label=f"{dep_level}% Deployment", color=colors[i])

    plt.title("Total Drag Coefficient vs. Timestep for Different Airbrake Deployments")
    plt.xlabel("Timestep")
    plt.ylabel("Total Drag Coefficient (Cd)")
    plt.grid(True, which="both", linestyle="--", linewidth=0.5)
    plt.legend(title="Airbrake Deployment", bbox_to_anchor=(1.02, 1), loc="upper left")
    plt.tight_layout(rect=[0, 0, 0.88, 1])

    # --- Plot 2: Body Cd Lookup Method Comparison ---
    print("Generating Plot 2: Body Cd Lookup Method Comparison...")
    plt.figure(figsize=(14, 8))

    # Lookup by velocity
    body_cds_from_vel = [
        body_lookup(vel, body_velocities, body_cds_baseline) for vel in body_velocities
    ]

    # Lookup by altitude
    body_cds_from_alt = [
        body_lookup(alt, body_altitudes, body_cds_baseline) for alt in body_altitudes
    ]

    plt.plot(
        timesteps,
        body_cds_from_vel,
        label="Body Cd from Velocity Lookup",
        color="blue",
        marker=".",
        linestyle="-"
    )
    plt.plot(
        timesteps,
        body_cds_from_alt,
        label="Body Cd from Altitude Lookup",
        color="red",
        marker="x",
        linestyle=":"
    )

    # For reference, plot the original data
    plt.plot(
        timesteps,
        body_cds_baseline,
        label="Original Body Cd Data",
        color="green",
        linestyle="--",
        alpha=0.7
    )

    plt.title("Comparison of Body Cd Lookup Methods")
    plt.xlabel("Timestep")
    plt.ylabel("Body Drag Coefficient (Cd)")
    plt.grid(True, which="both", linestyle="--", linewidth=0.5)
    plt.legend()
    plt.tight_layout()

    print("Displaying plots...")
    plt.show()


if __name__ == "__main__":
    # Check for dependencies
    try:
        import matplotlib
        import numpy
    except ImportError:
        print("This script requires 'matplotlib' and 'numpy'.")
        print("Please install them using: pip install matplotlib numpy")
        exit(1)

    main()