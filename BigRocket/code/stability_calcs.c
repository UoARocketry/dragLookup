#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef struct
{
    float velocity;
    float cp;
    float cg;
    float drag_force;
} FlightPoint;

const float airbrake_position = 112.0f;
const float rocket_diameter = 14.4f;
const float air_density_constant = 0.9428f;     // kg/m^3, from CFD
const float rocket_reference_area = 0.016259f;  // rocket cross-sectional area, m^2

const FlightPoint flight_data[] = {
    { 16.0611f, 148.0700f, 109.8000f, 1.3540f },
    { 29.6923f, 152.6522f, 109.8000f, 3.1107f },
    { 49.9778f, 153.0878f, 109.8000f, 7.7555f },
    { 70.0527f, 153.3921f, 109.8000f, 15.1047f },
    { 89.8388f, 154.5286f, 109.8000f, 25.3539f },
    { 109.4693f, 153.9853f, 109.8000f, 38.5721f },
    { 129.6832f, 154.3939f, 109.8000f, 56.2801f },
    { 149.9462f, 155.0857f, 109.8000f, 79.3479f },
    { 169.9826f, 155.7720f, 109.8000f, 108.2839f },
    { 190.5818f, 155.4821f, 109.8000f, 145.7725f },
    { 209.4175f, 156.5458f, 109.8000f, 187.5349f },
    { 229.4816f, 157.1160f, 109.8000f, 243.1708f },
    { 249.7206f, 158.1067f, 109.8000f, 316.1981f },
    { 269.7059f, 158.9143f, 109.8000f, 411.6920f },
    { 290.7387f, 159.8462f, 109.8000f, 553.5171f },
    { 309.6873f, 160.6611f, 109.8000f, 678.3124f },
    { 329.7954f, 161.5813f, 109.8000f, 758.1421f },
    { 344.1556f, 162.0571f, 109.8000f, 854.7373f },
};

const int flight_data_count = sizeof(flight_data) / sizeof(flight_data[0]);

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
    if (velocity < speeds[0])
        velocity = speeds[0];

    if (velocity > speeds[13])
        velocity = speeds[13];

    if (deployment < deployments[0])
        deployment = deployments[0];

    if (deployment > deployments[8])
        deployment = deployments[8];

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

    float cd11 = cd_table[v1][d1];
    float cd12 = cd_table[v1][d2];
    float cd21 = cd_table[v2][d1];
    float cd22 = cd_table[v2][d2];

    float deployment_fraction =
        (deployment - deployments[d1]) /
        (deployments[d2] - deployments[d1]);

    float cd_at_v1 =
        cd11 + (cd12 - cd11) * deployment_fraction;

    float cd_at_v2 =
        cd21 + (cd22 - cd21) * deployment_fraction;

    float velocity_fraction =
        (velocity - speeds[v1]) /
        (speeds[v2] - speeds[v1]);

    float final_cd =
        cd_at_v1 + (cd_at_v2 - cd_at_v1) * velocity_fraction;

    return final_cd;
}

float airbrake_drag(float velocity, float deployment)
{
    float airbrake_cd = airbrake_cd_lookup(velocity, deployment);

    float single_airbrake_drag = 0.5f * air_density_constant
                                * velocity * velocity
                                * airbrake_cd
                                * rocket_reference_area;

    // Three airbrakes total
    return single_airbrake_drag * 3.0f;
}

const FlightPoint *openrocket_lookup(float velocity)
{
    if (flight_data_count == 0)
    {
        return NULL;
    }

    int best_index = -1;
    float smallest_difference = 999999.0f;

    for (int i = 0; i < flight_data_count; i++)
    {
        float difference = fabsf(flight_data[i].velocity - velocity);

        if (difference < smallest_difference)
        {
            smallest_difference = difference;
            best_index = i;
        }
    }

    if (best_index == -1)
    {
        return NULL;
    }

    return &flight_data[best_index];
}

float calculate_stability(
    const FlightPoint *rocket,
    float deployment,
    float *out_new_cp)
{
    float airbrake_dragforce = airbrake_drag(
        rocket->velocity,
        deployment
    );

    float new_cp =
        (rocket->drag_force * rocket->cp +
         airbrake_dragforce * airbrake_position)
        / (rocket->drag_force + airbrake_dragforce);

    float stability =
        (new_cp - rocket->cg) / rocket_diameter;

    if (out_new_cp != NULL)
        *out_new_cp = new_cp;

    return stability;
}

int stability_check(const FlightPoint *rocket, float deployment)
{
    const float stability_threshold = 1.5f;

    float stability = calculate_stability(rocket, deployment, NULL);

    if (stability >= stability_threshold)
    {
        return 1;
    }

    return 0;
}

int get_stability_decision(
    float velocity,
    float deployment,
    float *out_stability_margin
)
{
    const FlightPoint *rocket = openrocket_lookup(velocity);

    if (rocket == NULL)
    {
        if (out_stability_margin != NULL)
            *out_stability_margin = 0.0f;
        return 0;
    }

    float stability = calculate_stability(rocket, deployment, NULL);

    if (out_stability_margin != NULL)
        *out_stability_margin = stability;

    return stability >= 1.7f;
}

int main(void)
{
    char input[64];
    float velocity;
    float deployment;

    printf("Type EXIT at any prompt to quit.\n\n");

    while (1)
    {
        printf("Enter velocity (m/s): ");
        if (scanf("%63s", input) != 1)
            break;

        if (strcmp(input, "EXIT") == 0)
            break;

        velocity = strtof(input, NULL);

        printf("Enter deployment (degrees): ");
        if (scanf("%63s", input) != 1)
            break;

        if (strcmp(input, "EXIT") == 0)
            break;

        deployment = strtof(input, NULL);

        float stability_margin;
        int stable = get_stability_decision(velocity, deployment, &stability_margin);

        printf("\n========================================\n");
        printf("       STABILITY CALCULATION\n");
        printf("========================================\n");
        printf("Velocity:          %.2f m/s\n", velocity);
        printf("Deployment:        %.2f degrees\n", deployment);
        printf("Stability margin:  %.3f calibres\n", stability_margin);
        printf("Status:            %s\n\n", stable ? "STABLE" : "UNSTABLE");
    }

    printf("Exiting.\n");

    return 0;
}