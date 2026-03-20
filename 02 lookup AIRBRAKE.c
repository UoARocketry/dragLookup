/*
 * AIRBRAKE lookup module
 *
 * Usage:
 * - Include this C file in a test build, or move function declarations into a
 *   header and compile this as a library unit.
 * - Main API:
 *     float airbrake_lookup(float altitude_m, float speed,
 *                           float deployment_percent,
 *                           AirbrakeLookupMetric metric);
 * - Convenience wrappers:
 *     airbrake_lookup_drag_coefficient(...)
 *     airbrake_lookup_drag_force(...)
 *
 * Behavior:
 * - altitude_m selects one density block via fixed altitude bands.
 * - speed is clamped to the selected block's speed axis bounds.
 * - deployment accepts either 0..1 or 0..100 input.
 * - deployment outside [0, 1] after normalization returns -1.0f.
 * - altitude outside [290, 515] m returns -1.0f.
 * - Inside a selected block, bilinear interpolation is applied over
 *   speed and deployment.
 */

#include <stddef.h>
#include <stdio.h>

#define cd_values values
#include "generated/airbrake_dragCoefficient_lookup.h"

enum { AIRBRAKE_DRAGCOEFF_DEPLOYMENT_COUNT_VALUE = AIRBRAKE_DEPLOYMENT_COUNT };

/*
 * Remap drag-force generated symbols so both auto-generated headers can
 * coexist in the same translation unit.
 */
#define AirbrakeTable AirbrakeForceTable
#define SPEEDS_1168 FORCE_SPEEDS_1168
#define SPEEDS_1162 FORCE_SPEEDS_1162
#define SPEEDS_1156 FORCE_SPEEDS_1156
#define SPEEDS_1150 FORCE_SPEEDS_1150
#define SPEEDS_1145 FORCE_SPEEDS_1145
#define VALUES_1168 FORCE_VALUES_1168
#define VALUES_1162 FORCE_VALUES_1162
#define VALUES_1156 FORCE_VALUES_1156
#define VALUES_1150 FORCE_VALUES_1150
#define VALUES_1145 FORCE_VALUES_1145
#include "generated/airbrake_dragForce_lookup.h"
#undef AIRBRAKE_DEPLOYMENT_COUNT
#undef AirbrakeTable
#undef SPEEDS_1168
#undef SPEEDS_1162
#undef SPEEDS_1156
#undef SPEEDS_1150
#undef SPEEDS_1145
#undef VALUES_1168
#undef VALUES_1162
#undef VALUES_1156
#undef VALUES_1150
#undef VALUES_1145

enum { AIRBRAKE_DRAGFORCE_DEPLOYMENT_COUNT_VALUE = 10 };

typedef enum {
  AIRBRAKE_LOOKUP_DRAG_COEFF = 0,
  AIRBRAKE_LOOKUP_DRAG_FORCE = 1,
} AirbrakeLookupMetric;

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

/* Accept deployment as either 0..1 or 0..100 and normalize to 0..1. */
static float normalize_deployment(float deployment_percent) {
  float deployment = deployment_percent;
  if (deployment > 1.0f) {
    deployment *= 0.01f;
  }

  return deployment;
}

/* Clamp speed to the valid axis range for the chosen density block. */
static float clamp_speed_to_table_range(const float* speeds, int speed_count,
                                        float speed) {
  if (speed_count <= 0) {
    return speed;
  }
  return clampf(speed, speeds[0], speeds[speed_count - 1]);
}

// Altitude bands map to density blocks in descending density order.
// Boundaries are [lower, upper), except the last band includes 515.
static int altitude_to_block_index(float altitude_m, int* block_index) {
  /* Map altitude to one lookup block index; return 0 when out of range. */
  if (altitude_m < 290.0f || altitude_m > 515.0f) {
    return 0;
  }

  if (altitude_m < 340.0f) {
    *block_index = 0;  // 1.168 density block
  } else if (altitude_m < 390.0f) {
    *block_index = 1;  // 1.162 density block
  } else if (altitude_m < 440.0f) {
    *block_index = 2;  // 1.156 density block
  } else if (altitude_m < 490.0f) {
    *block_index = 3;  // 1.150 density block
  } else {
    *block_index = 4;  // 1.145 density block
  }

  return 1;
}

/* Find the axis interval containing x and return interpolation factor t. */
static void find_bracket_ascending(const float* axis, int count, float x,
                                   int* i0, int* i1, float* t) {
  if (count <= 1) {
    *i0 = 0;
    *i1 = 0;
    *t = 0.0f;
    return;
  }

  if (x <= axis[0]) {
    *i0 = 0;
    *i1 = 0;
    *t = 0.0f;
    return;
  }

  if (x >= axis[count - 1]) {
    *i0 = count - 1;
    *i1 = count - 1;
    *t = 0.0f;
    return;
  }

  for (int i = 0; i < count - 1; ++i) {
    if (x >= axis[i] && x <= axis[i + 1]) {
      float span = axis[i + 1] - axis[i];
      *i0 = i;
      *i1 = i + 1;
      *t = (span == 0.0f) ? 0.0f : (x - axis[i]) / span;
      return;
    }
  }

  *i0 = count - 1;
  *i1 = count - 1;
  *t = 0.0f;
}

/* Convert deployment to its surrounding column indices in [0.1, 1.0]. */
static void find_deployment_bracket(float deployment_percent,
                                    int deployment_count, int* j0, int* j1,
                                    float* t) {
  const float deployment_min = 0.1f;
  const float deployment_max = 1.0f;

  float deployment = normalize_deployment(deployment_percent);
  deployment = clampf(deployment, deployment_min, deployment_max);

  if (deployment_count <= 1) {
    *j0 = 0;
    *j1 = 0;
    *t = 0.0f;
    return;
  }

  float step =
      (deployment_max - deployment_min) / (float)(deployment_count - 1);
  float position = (deployment - deployment_min) / step;
  int lower = (int)position;

  if (lower < 0) {
    lower = 0;
  }
  if (lower >= deployment_count - 1) {
    *j0 = deployment_count - 1;
    *j1 = deployment_count - 1;
    *t = 0.0f;
    return;
  }

  *j0 = lower;
  *j1 = lower + 1;
  *t = position - (float)lower;
}

/* Bilinear interpolation of drag coefficient for a single density block. */
static float sample_drag_coeff_block(const AirbrakeTable* block, float speed,
                                     float deployment_percent) {
  int i0 = 0, i1 = 0;
  float ts = 0.0f;
  int j0 = 0, j1 = 0;
  float td = 0.0f;

  float clamped_speed =
      clamp_speed_to_table_range(block->speeds, block->speedCount, speed);

  find_bracket_ascending(block->speeds, block->speedCount, clamped_speed, &i0,
                         &i1, &ts);
  find_deployment_bracket(deployment_percent,
                          AIRBRAKE_DRAGCOEFF_DEPLOYMENT_COUNT_VALUE, &j0, &j1,
                          &td);

  const int width = AIRBRAKE_DRAGCOEFF_DEPLOYMENT_COUNT_VALUE;
  const float* values = block->cd_values;
  float v00 = values[i0 * width + j0];
  float v01 = values[i0 * width + j1];
  float v10 = values[i1 * width + j0];
  float v11 = values[i1 * width + j1];

  float low_speed = lerpf(v00, v01, td);
  float high_speed = lerpf(v10, v11, td);
  return lerpf(low_speed, high_speed, ts);
}

/* Bilinear interpolation of drag force for a single density block. */
static float sample_drag_force_block(const AirbrakeForceTable* block,
                                     float speed, float deployment_percent) {
  int i0 = 0, i1 = 0;
  float ts = 0.0f;
  int j0 = 0, j1 = 0;
  float td = 0.0f;

  float clamped_speed =
      clamp_speed_to_table_range(block->speeds, block->speedCount, speed);

  find_bracket_ascending(block->speeds, block->speedCount, clamped_speed, &i0,
                         &i1, &ts);
  find_deployment_bracket(deployment_percent,
                          AIRBRAKE_DRAGFORCE_DEPLOYMENT_COUNT_VALUE, &j0, &j1,
                          &td);

  const int width = AIRBRAKE_DRAGFORCE_DEPLOYMENT_COUNT_VALUE;
  float v00 = block->values[i0 * width + j0];
  float v01 = block->values[i0 * width + j1];
  float v10 = block->values[i1 * width + j0];
  float v11 = block->values[i1 * width + j1];

  float low_speed = lerpf(v00, v01, td);
  float high_speed = lerpf(v10, v11, td);
  return lerpf(low_speed, high_speed, ts);
}

/* Dispatch lookup by metric after validating deployment and altitude. */
float airbrake_lookup(float altitude_m, float speed, float deployment_percent,
                      AirbrakeLookupMetric metric) {
  const float normalized_deployment = normalize_deployment(deployment_percent);
  if (normalized_deployment < 0.0f || normalized_deployment > 1.0f) {
    return -1.0f;
  }

  int block_index = 0;
  if (!altitude_to_block_index(altitude_m, &block_index)) {
    return -1.0f;
  }

  if (metric == AIRBRAKE_LOOKUP_DRAG_FORCE) {
    if (block_index >= AIRBRAKE_DRAGFORCE_DENSITY_BLOCKS) {
      return -1.0f;
    }
    return sample_drag_force_block(&AIRBRAKE_DRAGFORCE_TABLE[block_index],
                                   speed, deployment_percent);
  }

  if (block_index >= AIRBRAKE_DRAGCOEFF_DENSITY_BLOCKS) {
    return -1.0f;
  }
  return sample_drag_coeff_block(&AIRBRAKE_DRAGCOEFF_TABLE[block_index], speed,
                                 deployment_percent);
}

/* Convenience wrapper for coefficient lookup. */
float airbrake_lookup_drag_coefficient(float altitude_m, float speed,
                                       float deployment_percent) {
  return airbrake_lookup(altitude_m, speed, deployment_percent,
                         AIRBRAKE_LOOKUP_DRAG_COEFF);
}

/* Convenience wrapper for force lookup. */
float airbrake_lookup_drag_force(float altitude_m, float speed,
                                 float deployment_percent) {
  return airbrake_lookup(altitude_m, speed, deployment_percent,
                         AIRBRAKE_LOOKUP_DRAG_FORCE);
}

typedef struct {
  float altitude_m;
  float speed;
  float deployment_percent;
  AirbrakeLookupMetric metric;
  const char* label;
} AirbrakeLookupTestCase;

/* Local smoke-test harness for quick verification in standalone builds. */
int main(void) {
  const AirbrakeLookupTestCase tests[] = {
      {400.0f, 43.0f, 30.0f, AIRBRAKE_LOOKUP_DRAG_COEFF,
       "1) NORMAL: alt=400m, 43, 30%, drag coefficient"},
      {400.0f, 43.0f, 30.0f, AIRBRAKE_LOOKUP_DRAG_FORCE,
       "2) NORMAL: alt=400m, 43, 30%, drag force"},
      {500.0f, 0.0f, 50.0f, AIRBRAKE_LOOKUP_DRAG_COEFF,
       "3) NORMAL: alt=500m, 0, 50%, drag coefficient"},
      {300.0f, 65.0f, 60.0f, AIRBRAKE_LOOKUP_DRAG_FORCE,
       "4) NORMAL: alt=300m, 65, 60%, drag force"},
      {450.0f, 42.0f, 80.0f, AIRBRAKE_LOOKUP_DRAG_COEFF,
       "5) NORMAL: alt=450m, 42, 80%, drag coefficient"},
      {450.0f, 55.0f, 90.0f, AIRBRAKE_LOOKUP_DRAG_FORCE,
       "6) BOUND CHECK: alt=450m, 55, 90%, drag force"},
      {400.0f, 30.0f, 40.0f, AIRBRAKE_LOOKUP_DRAG_COEFF,
       "7) BOUND CHECK: alt=400m, 30, 40%, drag coefficient"},
      {400.0f, 80.0f, 110.0f, AIRBRAKE_LOOKUP_DRAG_FORCE,
       "8) BOUND CHECK: alt=400m, 80, 110%, drag force"},
      {500.0f, 50.0f, -10.0f, AIRBRAKE_LOOKUP_DRAG_COEFF,
       "9) BOUND CHECK: alt=500m, 50, -10%, drag coefficient"},
      {520.0f, 50.0f, 50.0f, AIRBRAKE_LOOKUP_DRAG_COEFF,
       "10) BOUND CHECK: alt=520m out-of-range -> error"},
      {500.0f, 25.0f, 80.0f, AIRBRAKE_LOOKUP_DRAG_FORCE,
       "11) BOUND CHECK: alt=500m, 25, 80%, drag force"},
  };

  const size_t count = sizeof(tests) / sizeof(tests[0]);
  for (size_t i = 0; i < count; ++i) {
    const AirbrakeLookupTestCase* tc = &tests[i];
    float result = airbrake_lookup(tc->altitude_m, tc->speed,
                                   tc->deployment_percent, tc->metric);
    const char* metric_name = (tc->metric == AIRBRAKE_LOOKUP_DRAG_FORCE)
                                  ? "drag force"
                                  : "drag coefficient";

    printf("%s\n", tc->label);
    printf("   result (%s) = %.9f\n", metric_name, result);
  }

  return 0;
}
