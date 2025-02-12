#include "generator.h"
#include <stdio.h>

int main()
{
    // Set up a biome generator that reflects the biome generation of
    // Minecraft 1.18.
    Generator g;
    setupGenerator(&g, MC_1_18, 0);

    // Seeds are internally represented as unsigned 64-bit integers.
    uint64_t seed;
    for (seed = 0; ; seed++)
    {
        // Apply the seed to the generator for the Overworld dimension.
        applySeed(&g, DIM_OVERWORLD, seed);

        // To get the biome at a single block position, we can use getBiomeAt().
        int scale = 1; // scale=1: block coordinates, scale=4: biome coordinates
        int x = 0, y = 63, z = 0;
        int biomeID = getBiomeAt(&g, scale, x, y, z);
        if (biomeID == mushroom_fields)
        {
            printf("Seed %" PRId64 " has a Mushroom Fields biome at "
                "block position (%d, %d).\n", (int64_t) seed, x, z);
            break;
        }
    }

    return 0;
}