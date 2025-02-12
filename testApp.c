#include "finders.h"
#include "generator.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int *biomeIds;
int mcVersion = MC_1_20;
int dimension = DIM_OVERWORLD;
unsigned char biomeColors[256][3];
int areaWidth = 1920;
int areaHeight = 1240;
int y = 4;

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <x> <z> <seed>\n", argv[0]);
        return 1;
    }

    int x = atoi(argv[1]);
    int z = atoi(argv[2]);
    int64_t seed = atoll(argv[3]);

    // Initialize biome colors
    initBiomeColors(biomeColors);
    
    // Setup generator
    Generator g;
    setupGenerator(&g, mcVersion, 0);
    
    // Setup range
    Range r = {4, x, z, areaWidth, areaHeight, y / 4, 1};
    
    // Allocate cache for biomes
    biomeIds = allocCache(&g, r);
    if (!biomeIds) {
        printf("Failed to allocate biome cache\n");
        return 1;
    }
    
    // Apply seed and generate biomes
    applySeed(&g, dimension, seed);
    genBiomes(&g, biomeIds, r);
    
    // Create the map image
    int pix4cell = 4;
    int imgWidth = pix4cell * r.sx;
    int imgHeight = pix4cell * r.sz;
    
    // Allocate memory for the RGB image
    unsigned char *rgb = (unsigned char *)malloc(3 * imgWidth * imgHeight);
    if (!rgb) {
        printf("Failed to allocate memory for image\n");
        free(biomeIds);
        return 1;
    }
    
    // Convert biomes to image
    biomesToImage(rgb, biomeColors, biomeIds, r.sx, r.sz, pix4cell, 2);
    
    // Save the image
    if (savePPM("map.ppm", rgb, imgWidth, imgHeight) != 0) {
        printf("Failed to save image\n");
    } else {
        printf("Map saved as map.ppm\n");
    }
    
    // Clean up
    free(rgb);
    free(biomeIds);
    
    return 0;
}
