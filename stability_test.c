#include <stdio.h>

double rocketForce = 2.484628728;
double airbrakeForce = 0.4946752589;
float baselineCP = 84.173;
float CG = 67.267;
float airbrakePosition = 65.7;
float diameter = 8.38;

struct StabilityResult {
    float newCP;
    float stability;
};

struct StabilityResult stabilityCalcs(void) {
    float newCP = (rocketForce * baselineCP +
                   airbrakeForce * airbrakePosition) /
                  (rocketForce + airbrakeForce);

    float stability = (newCP - CG) / diameter;

    struct StabilityResult result = {
        newCP,
        stability
    };

    return result;
}

int main(void) {
    struct StabilityResult result = stabilityCalcs();

    printf("New CP: %.3f\n", result.newCP);
    printf("Stability: %.3f\n", result.stability);

    return 0;
}