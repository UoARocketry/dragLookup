/*
 * BODY lookup module
 *
 * Usage:
 * - Main API:
 *     float body_lookup(float input_value, BodyLookupInput input_kind);
 * - Convenience wrappers:
 *     body_lookup_by_altitude(...)
 *     body_lookup_by_vertical_velocity(...)
 *
 * Behavior:
 * - Interpolates along a single selected axis (altitude sea level or velocity).
 * - Looks up drag coefficient from the input axis.
 * - Values outside axis bounds are clamped to the nearest endpoint.
 */

#include <stddef.h>
#include <stdio.h>

#include "generated/body_dragCoefficient_lookup.h"

typedef enum {
  BODY_LOOKUP_BY_ALTITUDE_SEALEVEL = 0,
  BODY_LOOKUP_BY_VERTICAL_VELOCITY = 1,
} BodyLookupInput;

typedef enum {
  static float clampf(float x, float min_value,
                      float max_value){if (x < min_value){return min_value;}
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
float body_lookup(float input_value, BodyLookupInput input_kind) {
  const float* axis = NULL;
  const float* values = NULL;
  const size_t count = BODY_DRAGCOEFF_ROW_COUNT;

  if (input_kind == BODY_LOOKUP_BY_ALTITUDE_SEALEVEL) {
    axis = BODY_ALTITUDE_SEALEVEL_M;
  } else {
    axis = BODY_VERTICAL_VELOCITY_MPS;
  }

  values = BODY_CD;

  return interpolate_monotonic_axis(axis, values, count, input_value);
}

/* Convenience wrapper: lookup using sea-level altitude as input axis. */
float body_lookup_by_altitude(float altitude_sealevel_m) {
  return body_lookup(altitude_sealevel_m, BODY_LOOKUP_BY_ALTITUDE_SEALEVEL);
}

/* Convenience wrapper: lookup using vertical velocity as input axis. */
float body_lookup_by_vertical_velocity(float vertical_velocity_mps) {
  return body_lookup(vertical_velocity_mps, BODY_LOOKUP_BY_VERTICAL_VELOCITY);
}

typedef struct {
  float input_value;
  BodyLookupInput input_kind;
  const char* label;
} BodyLookupTestCase;

int main(void) {
  const BodyLookupTestCase tests[] = {
      {331.148f, BODY_LOOKUP_BY_ALTITUDE_SEALEVEL,
       "1) 331.148, altitude, drag coefficient"},
      {55.6f, BODY_LOOKUP_BY_VERTICAL_VELOCITY,
       "2) 55.6, speed, drag coefficient"},
  };

  const size_t count = sizeof(tests) / sizeof(tests[0]);
  for (size_t i = 0; i < count; ++i) {
    const BodyLookupTestCase* tc = &tests[i];
    float result = body_lookup(tc->input_value, tc->input_kind);
    printf("%s\n", tc->label);
    printf("   result = %.9f\n", result);
  }

  return 0;
}
