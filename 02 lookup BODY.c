/*
 * BODY lookup module
 *
 * Usage:
 * - Main API:
 *     float body_lookup(float input_value, BodyLookupInput input_kind,
 *                       BodyLookupMetric metric);
 * - Convenience wrappers:
 *     body_lookup_by_altitude(...)
 *     body_lookup_by_vertical_velocity(...)
 *     body_drag_coefficient_from_altitude(...)
 *     body_drag_force_from_altitude(...)
 *     body_drag_coefficient_from_velocity(...)
 *     body_drag_force_from_velocity(...)
 *
 * Behavior:
 * - Interpolates along a single selected axis (altitude sea level or velocity).
 * - Supports drag coefficient and drag force from the same input axis.
 * - Values outside axis bounds are clamped to the nearest endpoint.
 */

#include <stddef.h>
#include <stdio.h>

#include "generated/body_dragCoefficient_lookup.h"

// Remap duplicated symbols so both generated headers can coexist.
#define BodyLookupRow BodyForceLookupRow
#define BODY_ALTITUDE_SEALEVEL_M BODY_FORCE_ALTITUDE_SEALEVEL_M
#define BODY_ALTITUDE_AGL_M BODY_FORCE_ALTITUDE_AGL_M
#define BODY_VERTICAL_VELOCITY_MPS BODY_FORCE_VERTICAL_VELOCITY_MPS
#include "generated/body_dragForce_lookup.h"
#undef BodyLookupRow
#undef BODY_ALTITUDE_SEALEVEL_M
#undef BODY_ALTITUDE_AGL_M
#undef BODY_VERTICAL_VELOCITY_MPS

_Static_assert(BODY_DRAGCOEFF_ROW_COUNT == BODY_DRAGFORCE_ROW_COUNT,
               "BODY coefficient/force table sizes must match.");

typedef enum {
  BODY_LOOKUP_BY_ALTITUDE_SEALEVEL = 0,
  BODY_LOOKUP_BY_VERTICAL_VELOCITY = 1,
} BodyLookupInput;

typedef enum {
  BODY_LOOKUP_DRAG_COEFFICIENT = 0,
  BODY_LOOKUP_DRAG_FORCE = 1,
} BodyLookupMetric;

/* Clamp a scalar to [min_value, max_value]. */
static float clampf(float x, float min_value, float max_value) {
  if (x < min_value) {
    return min_value;
  }
  if (x > max_value) {
    return max_value;
  }
  return x;
}

/* Linear interpolation between a and b using factor t in [0, 1]. */
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* Interpolate over monotonic axis data (ascending or descending). */
static float interpolate_monotonic_axis(const float* axis, const float* values,
                                        size_t count, float x) {
  if (count == 0) {
    return -1.0f;
  }
  if (count == 1) {
    return values[0];
  }

  const float first = axis[0];
  const float last = axis[count - 1];
  const int ascending = last >= first;

  float clamped_x = x;
  if (ascending) {
    clamped_x = clampf(clamped_x, first, last);
  } else {
    clamped_x = clampf(clamped_x, last, first);
  }

  for (size_t i = 0; i < count - 1; ++i) {
    const float a0 = axis[i];
    const float a1 = axis[i + 1];

    const int in_segment = ascending ? (clamped_x >= a0 && clamped_x <= a1)
                                     : (clamped_x <= a0 && clamped_x >= a1);

    if (in_segment) {
      const float span = a1 - a0;
      const float t = (span == 0.0f) ? 0.0f : (clamped_x - a0) / span;
      return lerpf(values[i], values[i + 1], t);
    }
  }

  return values[count - 1];
}

/* Select axis/metric arrays and run clamped 1D interpolation. */
float body_lookup(float input_value, BodyLookupInput input_kind,
                  BodyLookupMetric metric) {
  const float* axis = NULL;
  const float* values = NULL;
  const size_t count = BODY_DRAGCOEFF_ROW_COUNT;

  if (input_kind == BODY_LOOKUP_BY_ALTITUDE_SEALEVEL) {
    axis = BODY_ALTITUDE_SEALEVEL_M;
  } else {
    axis = BODY_VERTICAL_VELOCITY_MPS;
  }

  if (metric == BODY_LOOKUP_DRAG_FORCE) {
    values = BODY_DRAG_FORCE;
  } else {
    values = BODY_CD;
  }

  return interpolate_monotonic_axis(axis, values, count, input_value);
}

/* Convenience wrapper: lookup using sea-level altitude as input axis. */
float body_lookup_by_altitude(float altitude_sealevel_m,
                              BodyLookupMetric metric) {
  return body_lookup(altitude_sealevel_m, BODY_LOOKUP_BY_ALTITUDE_SEALEVEL,
                     metric);
}

/* Convenience wrapper: lookup using vertical velocity as input axis. */
float body_lookup_by_vertical_velocity(float vertical_velocity_mps,
                                       BodyLookupMetric metric) {
  return body_lookup(vertical_velocity_mps, BODY_LOOKUP_BY_VERTICAL_VELOCITY,
                     metric);
}

/* Lookup drag coefficient from sea-level altitude input. */
float body_drag_coefficient_from_altitude(float altitude_sealevel_m) {
  return body_lookup_by_altitude(altitude_sealevel_m,
                                 BODY_LOOKUP_DRAG_COEFFICIENT);
}

/* Lookup drag force from sea-level altitude input. */
float body_drag_force_from_altitude(float altitude_sealevel_m) {
  return body_lookup_by_altitude(altitude_sealevel_m, BODY_LOOKUP_DRAG_FORCE);
}

/* Lookup drag coefficient from vertical velocity input. */
float body_drag_coefficient_from_velocity(float vertical_velocity_mps) {
  return body_lookup_by_vertical_velocity(vertical_velocity_mps,
                                          BODY_LOOKUP_DRAG_COEFFICIENT);
}

/* Lookup drag force from vertical velocity input. */
float body_drag_force_from_velocity(float vertical_velocity_mps) {
  return body_lookup_by_vertical_velocity(vertical_velocity_mps,
                                          BODY_LOOKUP_DRAG_FORCE);
}

typedef struct {
  float input_value;
  BodyLookupInput input_kind;
  BodyLookupMetric metric;
  const char* label;
} BodyLookupTestCase;

int main(void) {
  const BodyLookupTestCase tests[] = {
      {331.148f, BODY_LOOKUP_BY_ALTITUDE_SEALEVEL, BODY_LOOKUP_DRAG_FORCE,
       "1) 331.148, altitude, drag force"},
      {331.148f, BODY_LOOKUP_BY_ALTITUDE_SEALEVEL, BODY_LOOKUP_DRAG_COEFFICIENT,
       "2) 331.148, altitude, drag coefficient"},
      {55.6f, BODY_LOOKUP_BY_VERTICAL_VELOCITY, BODY_LOOKUP_DRAG_FORCE,
       "3) 55.6, speed, drag force"},
      {55.6f, BODY_LOOKUP_BY_VERTICAL_VELOCITY, BODY_LOOKUP_DRAG_COEFFICIENT,
       "4) 55.6, speed, drag coefficient"},
  };

  const size_t count = sizeof(tests) / sizeof(tests[0]);
  for (size_t i = 0; i < count; ++i) {
    const BodyLookupTestCase* tc = &tests[i];
    float result = body_lookup(tc->input_value, tc->input_kind, tc->metric);
    printf("%s\n", tc->label);
    printf("   result = %.9f\n", result);
  }

  return 0;
}
