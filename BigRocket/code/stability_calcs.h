#ifndef STABILITY_H
#define STABILITY_H

// Checks whether the rocket remains stable at the given velocity and
// airbrake deployment angle (degrees), using the downsampled OpenRocket
// lookup table and CFD-derived airbrake Cd table compiled into stability.cpp.
//
// Returns 1 if stable (margin >= 1.7 calibres), 0 otherwise.
// If out_stability_margin is not NULL, the raw margin (in calibres) is
// also written to it.
int get_stability_decision(float velocity, float deployment, float *out_stability_margin);

#endif
