#include "generator.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

// Function to recursively create directories
int createDir(const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) && errno != EEXIST) {
                perror("Error creating directory");
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) && errno != EEXIST) {
        perror("Error creating directory");
        return -1;
    }
    return 0;
}

// Function to generate and save a single tile
void generateTile(Generator *g, uint64_t seed, int tileX, int tileZ, int tileSize, const char *outputDir) {
    // Set up the biome generator for Minecraft 1.18 with LARGE_BIOMES setting
    setupGenerator(g, MC_1_18, LARGE_BIOMES);
    applySeed(g, DIM_OVERWORLD, seed);

    // Define the range for the tile
    Range r;
    r.scale = 16; // Scale for biome coordinates
    r.x = tileX * tileSize; // Starting x coordinate
    r.z = tileZ * tileSize; // Starting z coordinate
    r.sx = tileSize; // Size of the tile
    r.sz = tileSize; // Size of the tile
    r.y = 15; // y and sy are typically not used in 2D generation
    r.sy = 1;

    // Allocate memory for storing biome IDs
    int *biomeIds = allocCache(g, r);
    genBiomes(g, biomeIds, r);

    // Parameters for image generation
    int pix4cell = 4; // Pixels per cell
    int imgWidth = pix4cell * r.sx;
    int imgHeight = pix4cell * r.sz;

    // Initialize colors for biomes
    unsigned char biomeColors[256][3];
    initBiomeColors(biomeColors);

    // Allocate memory for the image
    unsigned char *rgb = (unsigned char *)malloc(3 * imgWidth * imgHeight);

    // Convert biomes to image
    biomesToImage(rgb, biomeColors, biomeIds, r.sx, r.sz, pix4cell, 2);

    // Ensure the directory exists
    char tileDir[256];
    snprintf(tileDir, sizeof(tileDir), "%s/tile_%d_%d", outputDir, tileX, tileZ);
    if (createDir(tileDir) != 0) {
        free(biomeIds);
        free(rgb);
        return;
    }

    // Construct the output file path
    char outputFile[256];
    snprintf(outputFile, sizeof(outputFile), "%s/tile_%d_%d.png", tileDir, tileX, tileZ);

    // Save the image to a PNG file
    if (savePNG(outputFile, rgb, imgWidth, imgHeight) != 0) {
        fprintf(stderr, "Error saving PNG file for tile %d_%d\n", tileX, tileZ);
    } else {
        printf("Tile map generated and saved to %s\n", outputFile);
    }

    // Free allocated memory
    free(biomeIds);
    free(rgb);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <seed> <tile_size> <tiles_x> <tiles_z>\n", argv[0]);
        return 1;
    }

    // Parse arguments
    uint64_t seed = strtoull(argv[1], NULL, 10);
    int tileSize = atoi(argv[2]);
    int tilesX = atoi(argv[3]);
    int tilesZ = atoi(argv[4]);

    // Define the output directory
    const char *outputDir = "/var/www/storage/app/public/tiles"; // Local file path

    // Create the output directory if it doesn't exist
    if (createDir(outputDir) != 0) {
        return 1;
    }

    // Initialize the biome generator
    Generator g;
    setupGenerator(&g, MC_1_18, LARGE_BIOMES);

    // Generate and save each tile
    for (int x = -tilesX; x <= tilesX; ++x) {
        for (int z = -tilesZ; z <= tilesZ; ++z) {
            generateTile(&g, seed, x, z, tileSize, outputDir);
        }
    }

    return 0;
}
