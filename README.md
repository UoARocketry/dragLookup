# dragLookup

Lookup table generation and runtime lookup code for AIRBRAKE and BODY drag data.

Note: uses sea level altitude inputs. velocity has to be in the vertical axis.

## Project Files

- `01 generate header AIRBRAKE.py`: reads AIRBRAKE CSV tables and generates AIRBRAKE headers.
- `01 generate header BODY.py`: reads BODY CSV series and generates BODY headers.
- `02 lookup AIRBRAKE.c`: AIRBRAKE runtime lookup with interpolation/clamping/error rules.
- `02 lookup BODY.c`: BODY runtime lookup by sea-level altitude or vertical velocity.
- `csv/`: source CSV data.
- `generated/`: generated `.h` lookup headers.

## Header Generation (`01*` files)

### AIRBRAKE headers

Input format (per CSV):
- Row 1: altitude range
- Row 2: air density
- Row 3: deployment axis
- Row 4+: flow speed + 2D table values

Generate headers:

```bash
python "01 generate header AIRBRAKE.py" --plot none --write-headers --headers-dir generated
```

Outputs:
- `generated/airbrake_dragCoefficient_lookup.h`
- `generated/airbrake_dragForce_lookup.h`

Optional plotting:

```bash
python "01 generate header AIRBRAKE.py" --plot heatmap
python "01 generate header AIRBRAKE.py" --metric dragCoeff --plot lines
```

### BODY headers

Input format (per CSV):
- `Atitude (Sealevel)`
- `Atitude (AGL)`
- `Vertical Velocity (m/s)`
- `Drag Force` or `Drag Coefficient`

Generate headers:

```bash
python "01 generate header BODY.py" --pattern "BODY_*.csv" --headers-dir generated
```

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

Compile AIRBRAKE lookup:

```bash
gcc -std=c11 -Wall -Wextra -pedantic "02 lookup AIRBRAKE.c" -o /tmp/airbrake_lookup
```

Compile BODY lookup:

```bash
gcc -std=c11 -Wall -Wextra -pedantic "02 lookup BODY.c" -o /tmp/body_lookup
```
