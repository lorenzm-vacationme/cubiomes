#include "generator.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

void saveToFile(const char *filename, const char *text) {
    FILE *file = fopen(filename, "w");
    if (file) {
        fprintf(file, "%s", text);
        fclose(file);
    } else {
        perror("Error opening file for writing");
    }
}

int main() {
    // Set up a biome generator that reflects the biome generation of Minecraft 1.18.
    Generator g;
    setupGenerator(&g, MC_1_18, 0);

    // Seeds are internally represented as unsigned 64-bit integers.
    uint64_t seed;
    int found = 0; // Flag to indicate if Mushroom Fields biome is found

    for (seed = 0; ; seed++) {
        // Apply the seed to the generator for the Overworld dimension.
        applySeed(&g, DIM_OVERWORLD, seed);

        // To get the biome at a single block position, we can use getBiomeAt().
        int scale = 1; // scale=1: block coordinates, scale=4: biome coordinates
        int x = 0, y = 63, z = 0;
        int biomeID = getBiomeAt(&g, scale, x, y, z);
        if (biomeID == mushroom_fields) {
            found = 1;
            char json[1000]; // Assuming maximum JSON string length of 1000 characters
            snprintf(json, sizeof(json), "{\"seed\": %" PRId64 ", \"biome\": \"Mushroom Fields\", \"x\": %d, \"z\": %d}\n", (int64_t) seed, x, z);

            // Adjust the file path to save in the desired directory
            char filePath[1024]; // Use a buffer with sufficient length
            snprintf(filePath, sizeof(filePath), "storage/app/public/seeds/seed_%" PRId64 ".json", (int64_t) seed); // Use seed value in the filename

            saveToFile(filePath, json);
            break;
        }
    }

    if (!found) {
        printf("Mushroom Fields biome not found.\n");
    }

    return 0;   
}
