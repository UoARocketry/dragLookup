
#include <stdio.h>



const float speeds[] = {
    0, 1, 2, 3, 4, 5, 7.5, 10,
    24.9, 49.8, 99.6, 199.2, 298.7, 373.4
};

const float deployments[] = {
    10, 20, 30, 40, 50, 60, 70, 80, 90
};


const float cd_table[14][9] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0.009371, 0.0269008, 0.0502012, 0.0743548, 0.0962217, 0.115328, 0.130284, 0.14427, 0.156172},
    {0.0105118, 0.0293479, 0.0537171, 0.0778472, 0.0995396, 0.11823, 0.132821, 0.146461, 0.158466},
    {0.0110445, 0.0302669, 0.0549269, 0.0791383, 0.100955, 0.119494, 0.1341, 0.147633, 0.159765},
    {0.0114513, 0.030835, 0.0555749, 0.0798454, 0.101642, 0.120151, 0.134809, 0.148239, 0.160495},
    {0.0117771, 0.0312095, 0.0559371, 0.0803174, 0.102049, 0.12051, 0.135226, 0.148556, 0.160934},
    {0.0123596, 0.0316866, 0.0562445, 0.0808578, 0.102462, 0.120799, 0.13561, 0.148828, 0.161344},
    {0.0127732, 0.0319326, 0.0563767, 0.0811086, 0.102646, 0.120912, 0.135738, 0.148976, 0.16151},
    {0.0144511, 0.0332509, 0.0577251, 0.0825171, 0.103851, 0.121976, 0.13681, 0.150187, 0.162803},
    {0.0157244, 0.0348664, 0.0587828, 0.0835505, 0.104826, 0.122811, 0.137572, 0.150946, 0.163744},
    {0.0168058, 0.0365437, 0.0597223, 0.0844506, 0.105624, 0.12352, 0.138186, 0.151484, 0.164393},
    {0.0176375, 0.0374119, 0.0606057, 0.085156, 0.10624, 0.124052, 0.138634, 0.15187, 0.164839},
    {0.0180577, 0.0383909, 0.0609662, 0.0855134, 0.106557, 0.124284, 0.138846, 0.152049, 0.165018},
    {0.0182746, 0.0382504, 0.061185, 0.0856915, 0.106712, 0.1244, 0.138951, 0.152138, 0.1651}
};

float airbrake_cd_lookup(float velocity, float deployment)
{
    // ---------------------------------------------------------
    // 1. Keep the inputs within the range of our CFD data
    // ---------------------------------------------------------
    // CFD data only covers:
    // Velocity:   0 to 373.4 m/s
    // Deployment: 10 to 90 degrees

    if (velocity < speeds[0])
        velocity = speeds[0];

    if (velocity > speeds[13])
        velocity = speeds[13];

    if (deployment < deployments[0])
        deployment = deployments[0];

    if (deployment > deployments[8])
        deployment = deployments[8];


    // ---------------------------------------------------------
    // 2. Find the two velocity values surrounding our input
    // ---------------------------------------------------------
    // For example, if velocity = 50 m/s:
    //
    // 49.8 m/s < 50 m/s < 99.6 m/s
    //
    // Therefore we need the Cd values at 49.8 and 99.6 m/s.

    int velocity_index = 0;

    for (int i = 0; i < 13; i++)
    {
        if (velocity >= speeds[i] && velocity <= speeds[i + 1])
        {
            velocity_index = i;
            break;
        }
    }

    int v1 = velocity_index;
    int v2 = velocity_index + 1;


    // ---------------------------------------------------------
    // 3. Find the two deployment values surrounding our input
    // ---------------------------------------------------------
    // For example, if deployment = 55 degrees:
    //
    // 50 degrees < 55 degrees < 60 degrees
    //
    // So we need the Cd values at 50 and 60 degrees.

    int deployment_index = 0;

    for (int i = 0; i < 8; i++)
    {
        if (deployment >= deployments[i] &&
            deployment <= deployments[i + 1])
        {
            deployment_index = i;
            break;
        }
    }

    int d1 = deployment_index;
    int d2 = deployment_index + 1;


    // ---------------------------------------------------------
    // 4. Get the four surrounding Cd values
    // ---------------------------------------------------------
    //
    //             d1          d2
    //              |           |
    // v1 -------- Cd11 ------- Cd12
    //              |           |
    // v2 -------- Cd21 ------- Cd22
    //
    // These four values surround the velocity/deployment
    // combination that we're looking for.

    float cd11 = cd_table[v1][d1];
    float cd12 = cd_table[v1][d2];
    float cd21 = cd_table[v2][d1];
    float cd22 = cd_table[v2][d2];


    // ---------------------------------------------------------
    // 5. Interpolate in the deployment direction
    // ---------------------------------------------------------

    float deployment_fraction =
        (deployment - deployments[d1]) /
        (deployments[d2] - deployments[d1]);

    float cd_at_v1 =
        cd11 + (cd12 - cd11) * deployment_fraction;

    float cd_at_v2 =
        cd21 + (cd22 - cd21) * deployment_fraction;


    // ---------------------------------------------------------
    // 6. Interpolate in the velocity direction
    // ---------------------------------------------------------

    float velocity_fraction =
        (velocity - speeds[v1]) /
        (speeds[v2] - speeds[v1]);

    float final_cd =
        cd_at_v1 + (cd_at_v2 - cd_at_v1) * velocity_fraction;


    // Return the estimated airbrake drag coefficient
    return final_cd;
}


int main(void)
{
    float velocity = 50.0;
    float deployment = 55.0;

    float cd = airbrake_cd_lookup(velocity, deployment);

    printf("Velocity: %.2f m/s\n", velocity);
    printf("Deployment: %.2f degrees\n", deployment);
    printf("Airbrake Cd: %.6f\n", cd);

    return 0;
}


float airbrake_drag(float air_density, float velocity,
                    float deployment, float airbrake_area)
{
    // Get the airbrake Cd from the CFD lookup table
    float airbrake_cd = airbrake_cd_lookup(velocity, deployment);

    // Calculate aerodynamic drag force:
    // F = 0.5 * rho * V^2 * Cd * A
    float ab_drag = 0.5f * air_density
                  * velocity * velocity
                  * airbrake_cd
                  * airbrake_area;

    return ab_drag;
}