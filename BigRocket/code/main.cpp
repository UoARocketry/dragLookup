#include <Arduino.h>
#include "stability_calcs.h"

void setup()
{
    Serial.begin(115200);
    delay(1000);  // give the serial monitor a moment to connect

    // --- Example usage ---
    // Replace these with real velocity/deployment readings once
    // the flight computer's sensor pipeline is wired up.

    float test_velocity = 200.0f;
    float test_deployment = 45.0f;

    float margin;
    int stable = get_stability_decision(test_velocity, test_deployment, &margin);

    Serial.println("=== Stability check example ===");
    Serial.print("Velocity: ");
    Serial.print(test_velocity);
    Serial.println(" m/s");
    Serial.print("Deployment: ");
    Serial.print(test_deployment);
    Serial.println(" deg");
    Serial.print("Margin: ");
    Serial.print(margin, 3);
    Serial.println(" calibres");
    Serial.println(stable ? "Status: STABLE" : "Status: UNSTABLE");
}

void loop()
{
    // Real usage will call get_stability_decision() here on a timer or
    // in response to new sensor data, e.g.:
    //
    //   float margin;
    //   int stable = get_stability_decision(current_velocity, current_deployment, &margin);
    //   if (!stable) { /* take action */ }
}