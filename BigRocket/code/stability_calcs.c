#include <stdio.h>
#include <math.h>

#define MAX_ROWS 1200

// One row of useful OpenRocket flight data
typedef struct
{
    float time;
    float altitude;
    float velocity;
    float angle_of_attack;
    float cp;
    float cg;
    float drag_force;
    float drag_coefficient;
    float air_pressure;
    float air_density;
    float mach;

} OpenRocketData;


// Array to hold the OpenRocket simulation
OpenRocketData flight_data[MAX_ROWS];

int flight_data_count = 0;

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


int load_openrocket_data(const char *filename)
{
    FILE *file = fopen(filename, "r");

    if (file == NULL)
    {
        printf("ERROR: Could not open OpenRocket CSV\n");
        return 0;
    }

    char line[500];

    while (fgets(line, sizeof(line), file) != NULL)
    {
        // OpenRocket uses # for comments, events and headers.
        // We don't want to try to interpret those as data.
        if (line[0] == '#')
            continue;

        // Ignore blank lines
        if (line[0] == '\n')
            continue;

        if (flight_data_count >= MAX_ROWS)
        {
            printf("ERROR: Too many OpenRocket rows\n");
            fclose(file);
            return 0;
        }

        OpenRocketData *row = &flight_data[flight_data_count];

        int result = sscanf(
            line,
            "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
            &row->time,
            &row->altitude,
            &row->velocity,
            &row->angle_of_attack,
            &row->cp,
            &row->cg,
            &row->drag_force,
            &row->drag_coefficient,
            &row->air_pressure,
            &row->air_density,
            &row->mach
        );

        // Only add the row if all 11 values were successfully read
        if (result == 11)
        {
            flight_data_count++;
        }
    }

    fclose(file);

    printf("Loaded %d OpenRocket data points\n", flight_data_count);

    return 1;
}


OpenRocketData *openrocket_lookup(float velocity)
{
    if (flight_data_count == 0)
    {
        return NULL;
    }

    int best_index = -1;
    float smallest_difference = 999999.0f;

    for (int i = 0; i < flight_data_count; i++)
    {
        // Ignore rows where OpenRocket hasn't calculated a CP
        // (these occur before launch / after apogee)
        if (isnan(flight_data[i].cp))
            continue;

        float difference = fabsf(
            flight_data[i].velocity - velocity
        );

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


const float area_deployments[] = {
    10, 20, 30, 40, 50, 60, 70, 80, 90
};

const float airbrake_areas[] = {
    0.000388,
    0.000734,
    0.001043,
    0.001316,
    0.001556,
    0.001766,
    0.001948,
    0.002106,
    0.002240
};


float airbrake_area_lookup(float deployment)
{
    // Keep deployment within the range of our area data
    if (deployment < area_deployments[0])
        deployment = area_deployments[0];

    if (deployment > area_deployments[8])
        deployment = area_deployments[8];

    // Find the two deployment angles surrounding our input
    int index = 0;

    for (int i = 0; i < 8; i++)
    {
        if (deployment >= area_deployments[i] &&
            deployment <= area_deployments[i + 1])
        {
            index = i;
            break;
        }
    }

    // Get the two surrounding area values
    float area1 = airbrake_areas[index];
    float area2 = airbrake_areas[index + 1];

    // Calculate how far between the two deployment angles we are
    float fraction =
        (deployment - area_deployments[index]) /
        (area_deployments[index + 1] - area_deployments[index]);

    // Linearly interpolate the area
    return area1 + (area2 - area1) * fraction;
}


float airbrake_drag(float air_density, float velocity,
                    float deployment)
{
    // Look up aerodynamic properties from the CFD-derived tables
    float airbrake_cd = airbrake_cd_lookup(velocity, deployment);
    float airbrake_area = airbrake_area_lookup(deployment);

    // Calculate airbrake drag force:
    // F = 0.5 * rho * V^2 * Cd * A
    float ab_drag = 0.5f * air_density
                  * velocity * velocity
                  * airbrake_cd
                  * airbrake_area;

    return ab_drag;
}



int main(void)
{
    // Load the OpenRocket CSV
    if (!load_openrocket_data("../csv_data/Comprocket data.csv"))
    {
        return 1;
    }

    // Test velocity
    float velocity = 50.0f;

    // Find the closest OpenRocket row
    OpenRocketData *rocket = openrocket_lookup(velocity);

    if (rocket == NULL)
    {
        printf("Could not find OpenRocket data\n");
        return 1;
    }

    printf("\nOpenRocket lookup:\n");

    printf("Time:           %.3f s\n", rocket->time);
    printf("Altitude:       %.3f m\n", rocket->altitude);
    printf("Velocity:       %.3f m/s\n", rocket->velocity);
    printf("CP:             %.3f cm\n", rocket->cp);
    printf("CG:             %.3f cm\n", rocket->cg);
    printf("Drag force:     %.3f N\n", rocket->drag_force);
    printf("Drag coefficient: %.4f\n", rocket->drag_coefficient);
    printf("Air density:    %.6f\n", rocket->air_density);
    printf("Mach:           %.3f\n", rocket->mach);

    return 0;
}