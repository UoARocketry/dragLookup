# dragLookup

This project provides tools to generate C header files from drag coefficient lookup tables and C functions to perform runtime lookups against that data. It supports separate lookups for the rocket body and airbrakes, and provides a combined calculation.

The system is designed to use sea-level altitude and vertical velocity as primary inputs.

## Simulation Assumptions:
- Airbrakes: The CFD model uses agl altitude inputs, so the agl sea level difference is already accounted for in its data.
- Body: This is derived from open rocket, which outputs sea level altitudes, so the agl sea level difference is not accounted for in its data. When compiling the BODY lookup table, the agl sea level difference must be provided to the header generation script. For the Orini launch site, this is 40m.

## Project Files

- `01 generate header AIRBRAKE.py`: reads AIRBRAKE CSV tables and generates AIRBRAKE headers.
- `01 generate header BODY.py`: reads BODY CSV series and generates BODY headers.
- `02 lookup COMBINED.c`: Combined runtime lookup for AIRBRAKE and BODY with interpolation, clamping, and error handling.
- `csv/`: source CSV data.
- `generated/`: generated `.h` lookup headers.

## Header Generation (`01*` files)

The Python scripts in the root directory parse CSV files and generate C header files containing the lookup tables.

### AIRBRAKE headers

Direct copy pastes from the .xlsx file. Open both in excel and copy the relevant cells into a CSV file. The first 4 rows are metadata, and the rest is a 2D table of values.

Input format (per CSV):
- Row 1: altitude range
- Row 2: air density
- Row 3: deployment axis
- Row 4+: flow speed + 2D table values

Generate headers:

```bash
python "01 generate header AIRBRAKE.py" --write-headers --headers-dir generated
```

Outputs:
- `generated/airbrake_dragCoefficient_lookup.h`

Optional plotting:

```bash
python "01 generate header AIRBRAKE.py" --plot heatmap
python "01 generate header AIRBRAKE.py" --plot lines
```

### BODY headers

Have to remove the AGL column.

Input format (per CSV):
- `Atitude (Sealevel)`
- `Vertical Velocity (m/s)`
- `Drag Coefficient`

Generate headers:

```bash
python "01 generate header BODY.py" --pattern "BODY_*.csv" --headers-dir generated --agl-sealevel-diff 40.0
```

Note: agl sealevel diff is 40m for the Orini Launch Site

Outputs:
- `generated/body_dragCoefficient_lookup.h`
- `generated/body_dragForce_lookup.h`

## Header Lookup/Search (`02*` files)

### AIRBRAKE lookup (`02 lookup AIRBRAKE.c`)

Main API:

```c
float airbrake_lookup(float altitude_m, float speed, float deployment_percent,
                      AirbrakeLookupMetric metric);
```

Metric options:
- `AIRBRAKE_LOOKUP_DRAG_COEFF`
- `AIRBRAKE_LOOKUP_DRAG_FORCE`

Behavior summary:
- Altitude selects a fixed density block.
- Speed clamps to block min/max.
- Deployment accepts `0..1` or `0..100`.
- Deployment out of range returns `-1.0f`.
- Altitude out of range returns `-1.0f`.
- Bilinear interpolation is applied over speed and deployment in the selected block.

### BODY lookup (`02 lookup BODY.c`)

Main API:

```c
float body_lookup(float input_value, BodyLookupInput input_kind,
                  BodyLookupMetric metric);
```

Input axis options:
- `BODY_LOOKUP_BY_ALTITUDE_SEALEVEL`
- `BODY_LOOKUP_BY_VERTICAL_VELOCITY`

Metric options:
- `BODY_LOOKUP_DRAG_COEFFICIENT`
- `BODY_LOOKUP_DRAG_FORCE`

Behavior summary:
- 1D interpolation on the selected axis.
- Axis bounds are clamped to endpoint values.
- Altitude mode uses sea-level altitude axis from the generated headers.

## Build Checks

Compile lookup COMBINED .c:

```bash
gcc -std=c11 -Wall -Wextra -pedantic "02 lookup COMBINED.c" -o /tmp/airbrake_lookup
```
