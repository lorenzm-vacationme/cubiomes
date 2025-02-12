// find spawn and the first N strongholds
#include "generator.h"
#include "finders.h"
#include "layers.h"
#include <stdio.h>
#include <stdlib.h>  // For atoi and strtoull
#include <inttypes.h>  // For PRId64



int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <seed> [<number of strongholds>]\n", argv[0]);
        return 1;
    }

    int mc = MC_1_18;
    uint64_t seed = strtoull(argv[1], NULL, 10);  // Convert the first argument to a uint64_t
    int N = (argc > 2) ? atoi(argv[2]) : 12;  // Default to 12 strongholds if not provided

    if (N <= 0) {
        fprintf(stderr, "Invalid number of strongholds specified. Using default of 12.\n");
        N = 12;
    }

    // Only the first stronghold has a position that can be estimated
    // (+/-112 blocks) without biome check.
    StrongholdIter sh;
    Pos pos = initFirstStronghold(&sh, mc, seed);

    printf("Seed: %" PRId64 "\n", (int64_t) seed);
    printf("Estimated position of first stronghold: (%d, %d)\n", pos.x, pos.z);

    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    pos = getSpawn(&g);
    printf("Spawn: (%d, %d)\n", pos.x, pos.z);

    for (int i = 1; i <= N; i++)
    {
        if (nextStronghold(&sh, &g) <= 0)
            break;
        printf("Stronghold #%-3d: (%6d, %6d)\n", i, sh.pos.x, sh.pos.z);
    }

    return 0;
}
