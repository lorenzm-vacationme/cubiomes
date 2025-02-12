#include "generator.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include "finders.h"

// Function to get the spawn point
Pos getWorldSpawn(int seed)
{
    static Pos pos; // Avoid returning stack memory
    Generator g;
    setupGenerator(&g, MC_1_18, 0);
    uint64_t worldSeed = (uint64_t) seed;
    applySeed(&g, DIM_OVERWORLD, worldSeed);
    pos = getSpawn(&g);
    return pos;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: ./get_spawn <seed>\n");
        return 1;
    }

    int seed = atoi(argv[1]);
    Pos spawn = getWorldSpawn(seed);

    // Print the spawn coordinates (Can be used by another program)
    printf("Spawn Point: x=%d, z=%d\n", spawn.x, spawn.z);

    return 0;
}
