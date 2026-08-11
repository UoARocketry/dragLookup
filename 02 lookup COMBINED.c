/*
 * COMBINED AIRBRAKE and BODY lookup module
 *
 * Usage:
 * - Include this C file in a test build, or move function declarations into a
 *   header and compile this as a library unit.
 * - Main APIs:
 *     float airbrake_lookup(float altitude_m, float speed,
 *                           float deployment_percent);
 *     float body_lookup(float input_value, BodyLookupInput input_kind);
 *
 * Behavior:
 * - See individual function blocks for detailed behavior.
 */

#include <stddef.h>
#include <stdio.h>

// Include both generated lookup table headers.
#include "generated/airbrake_dragCoefficient_lookup.h"
#include "generated/body_dragCoefficient_lookup.h"

/*******************************************************************************
 * Common Helper Functions
 ******************************************************************************/

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

/*******************************************************************************
 * AIRBRAKE Lookup
 ******************************************************************************/

/* Accept deployment as either 0..1 or 0..100 and normalize to 0..1. */
static float airbrake_normalize_deployment(float deployment_percent) {
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
  if (count <= 1 || x <= axis[0]) {
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

  float deployment = airbrake_normalize_deployment(deployment_percent);
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
  find_deployment_bracket(deployment_percent, AIRBRAKE_DEPLOYMENT_COUNT, &j0,
                          &j1, &td);

  const int width = AIRBRAKE_DEPLOYMENT_COUNT;
  const float* values = block->cd_values;
  float v00 = values[i0 * width + j0];
  float v01 = values[i0 * width + j1];
  float v10 = values[i1 * width + j0];
  float v11 = values[i1 * width + j1];

  float low_speed = lerpf(v00, v01, td);
  float high_speed = lerpf(v10, v11, td);
  return lerpf(low_speed, high_speed, ts);
}

/* Main AIRBRAKE lookup API. */
float airbrake_lookup(float altitude_m, float speed, float deployment_percent) {
  const float normalized_deployment =
      airbrake_normalize_deployment(deployment_percent);
  if (normalized_deployment < 0.0f || normalized_deployment > 1.0f) {
    return -1.0f;
  }

  int block_index = 0;
  if (!altitude_to_block_index(altitude_m, &block_index)) {
    return -1.0f;
  }

  if (block_index >= AIRBRAKE_DRAGCOEFF_DENSITY_BLOCKS) {
    return -1.0f;
  }
  return sample_drag_coeff_block(&AIRBRAKE_DRAGCOEFF_TABLE[block_index], speed,
                                 deployment_percent);
}

/*******************************************************************************
 * BODY Lookup
 ******************************************************************************/

typedef enum {
  BODY_LOOKUP_BY_ALTITUDE_SEALEVEL = 0,
  BODY_LOOKUP_BY_VERTICAL_VELOCITY = 1,
} BodyLookupInput;

/* Interpolate over monotonic axis data (ascending or descending). */
static float interpolate_monotonic_axis(const float* axis, const float* values,
                                        size_t count, float x) {
  if (count == 0) return -1.0f;
  if (count == 1) return values[0];

  const float first = axis[0];
  const float last = axis[count - 1];
  const int ascending = last >= first;

  float clamped_x = ascending ? clampf(x, first, last) : clampf(x, last, first);

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

/* Main BODY lookup API. */
float body_lookup(float input_value, BodyLookupInput input_kind) {
  const float* axis = NULL;
  const size_t count = BODY_DRAGCOEFF_ROW_COUNT;

  if (input_kind == BODY_LOOKUP_BY_ALTITUDE_SEALEVEL) {
    axis = BODY_ALTITUDE_SEALEVEL_M;
  } else {
    axis = BODY_VERTICAL_VELOCITY_MPS;
  }

  return interpolate_monotonic_axis(axis, BODY_CD, count, input_value);
}

/*******************************************************************************
 * Combined Lookup
 ******************************************************************************/

/*
 * Calculates the total drag coefficient by combining airbrake and body values.
 *
 * Takes altitude, speed, and airbrake deployment as input.
 * Returns -1.0f if any of the underlying lookups fail.
 */
float get_total_drag_coefficient(float altitude_m, float speed,
                                 float deployment_percent) {
  // Perform a 2D lookup for the airbrake's drag coefficient.
  float airbrake_cd = airbrake_lookup(altitude_m, speed, deployment_percent);
  printf("Single Airbrake Cd is: %.4f\n", airbrake_cd);
  if (airbrake_cd < 0.0f) {
    return -1.0f;  // Propagate error from airbrake lookup.
  }

  // Perform a 1D lookup for the body's drag coefficient.
  // Per request, the primary lookup is by velocity.
  float body_cd = body_lookup(speed, BODY_LOOKUP_BY_VERTICAL_VELOCITY);
  printf("Body Cd is: %.4f\n", body_cd);

  /*
  // Alternate body lookup using altitude:
  float body_cd_alt = body_lookup(altitude_m, BODY_LOOKUP_BY_ALTITUDE_SEALEVEL);
  if (body_cd_alt < 0.0f) {
      return -1.0f;
  }
  */

  if (body_cd < 0.0f) {
    return -1.0f;  // Propagate error from body lookup.
  }

  // Combine the two values using the specified formula.
  printf("Combined Cd is: %.4f\n", (airbrake_cd * 3.0) + body_cd);
  return (airbrake_cd * 3.0f) + body_cd;
}

/*******************************************************************************
 * Main Test Harness
 ******************************************************************************/

typedef struct {
  float altitude_m;
  float speed;
  float deployment_percent;
  const char* label;
} AirbrakeLookupTestCase;

typedef struct {
  float input_value;
  BodyLookupInput input_kind;
  const char* label;
} BodyLookupTestCase;

int main(void) {
  printf("--- Running AIRBRAKE tests ---\n");
  const AirbrakeLookupTestCase airbrake_tests[] = {
      {400.0f, 43.0f, 30.0f, "1) NORMAL: alt=400m, 43, 30%"},
      {500.0f, 0.0f, 50.0f, "2) NORMAL: alt=500m, 0, 50%"},
      {450.0f, 42.0f, 80.0f, "3) NORMAL: alt=450m, 42, 80%"},
      {400.0f, 30.0f, 40.0f, "4) BOUND CHECK: alt=400m, 30, 40%"},
      {500.0f, 50.0f, -10.0f, "5) BOUND CHECK: alt=500m, 50, -10%"},
      {520.0f, 50.0f, 50.0f, "6) BOUND CHECK: alt=520m out-of-range -> error"},
  };
  const size_t airbrake_count =
      sizeof(airbrake_tests) / sizeof(airbrake_tests[0]);
  for (size_t i = 0; i < airbrake_count; ++i) {
    const AirbrakeLookupTestCase* tc = &airbrake_tests[i];
    float result =
        airbrake_lookup(tc->altitude_m, tc->speed, tc->deployment_percent);
    printf("%s\n", tc->label);
    printf("   result (drag coefficient) = %.9f\n", result);
  }

  printf("\n--- Running BODY tests ---\n");
  const BodyLookupTestCase body_tests[] = {
      {331.148f, BODY_LOOKUP_BY_ALTITUDE_SEALEVEL,
       "1) 331.148, altitude, drag coefficient"},
      {55.6f, BODY_LOOKUP_BY_VERTICAL_VELOCITY,
       "2) 55.6, speed, drag coefficient"},
  };
  const size_t body_count = sizeof(body_tests) / sizeof(body_tests[0]);
  for (size_t i = 0; i < body_count; ++i) {
    const BodyLookupTestCase* tc = &body_tests[i];
    float result = body_lookup(tc->input_value, tc->input_kind);
    printf("%s\n", tc->label);
    printf("   result (drag coefficient) = %.9f\n", result);
  }

  printf("\n--- Running Total Drag Coefficient tests ---\n");
  // Test case 1: Valid inputs
  float total_cd_test_1 = get_total_drag_coefficient(400.0f, 55.6f, 30.0f);
  printf("Total CD for alt=400m, speed=55.6, deployment=30 is %.4f\n",
         total_cd_test_1);

  // Test case 2: Invalid altitude to trigger an error
  float total_cd_test_2 = get_total_drag_coefficient(999.0f, 55.6f, 30.0f);
  printf("Total CD for alt=999m (error case) is %.9f\n", total_cd_test_2);

  return 0;
}