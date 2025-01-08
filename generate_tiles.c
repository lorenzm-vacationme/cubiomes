#include "generator.h"
#include "util.h"
#include "image_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// Function to create directories as needed
int createDir(const char *path) {
    char tmp[2048];
    char *p = tmp;
    snprintf(tmp, sizeof(tmp), "%s", path);

    // Handle absolute and relative paths consistently
    if (tmp[0] == '/') {
        p = tmp + 1;
    } else {
        p = tmp;
    }

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            // Create the directory if it doesn't exist
            if (mkdir(tmp, 0777) && errno != EEXIST) {
                fprintf(stderr, "Error creating directory %s: %s\n", tmp, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }

    // Final directory creation
    if (mkdir(tmp, 0777) && errno != EEXIST) {
        fprintf(stderr, "Error creating directory %s: %s\n", tmp, strerror(errno));
        return -1;
    }

    return 0;
}

// Structure to hold parameters for tile generation
typedef struct {
    Generator *g;
    uint64_t seed;
    int tileX;
    int tileY;
    int tileSize;
    const char *outputDir;
    int zoomLevel;
    int scale;
} TileParams;

// Function to generate a single tile based on OpenLayers request parameters
void generateTile(TileParams *params) {
    Generator *g = params->g;
    uint64_t seed = params->seed;
    int tileX = params->tileX;
    int tileY = params->tileY;
    int tileSize = params->tileSize;
    const char *outputDir = params->outputDir;
    int zoomLevel = params->zoomLevel;
    int scale = params->scale;

    setupGenerator(g, MC_1_18, LARGE_BIOMES);
    applySeed(g, DIM_OVERWORLD, seed);

    Range r = {
        .scale = scale,
        .x = tileX * tileSize,
        .z = tileY * tileSize,
        .sx = tileSize,
        .sz = tileSize,
        .y = 15,
        .sy = 1
    };

    int *biomeIds = allocCache(g, r);
    if (!biomeIds) {
        fprintf(stderr, "Error allocating memory for biomes\n");
        return;
    }

    genBiomes(g, biomeIds, r);

    int pix4cell = 4;
    int imgWidth = pix4cell * r.sx;
    int imgHeight = pix4cell * r.sz;

    unsigned char *rgb = (unsigned char *)malloc(3 * imgWidth * imgHeight);
    if (!rgb) {
        fprintf(stderr, "Error allocating memory for image\n");
        free(biomeIds); // Ensure biomeIds is freed
        return;
    }

    unsigned char biomeColors[256][3];
    initBiomeColors(biomeColors);

    biomesToImage(rgb, biomeColors, biomeIds, r.sx, r.sz, pix4cell, 2);

    char tileDir[4096], outputFile[8192];
    snprintf(tileDir, sizeof(tileDir), "%s/%lu/%d/%d", outputDir, seed, zoomLevel, tileX);
    snprintf(outputFile, sizeof(outputFile), "%s/%d.png", tileDir, tileY);

    if (createDir(tileDir) != 0 || savePNG(outputFile, rgb, imgWidth, imgHeight) != 0) {
        fprintf(stderr, "Error saving image file for tile %d_%d at zoom level %d\n", tileX, tileY, zoomLevel);
    } else {
        printf("Tile %d_%d at zoom level %d generated and saved to %s\n", tileX, tileY, zoomLevel, outputFile);
    }

    // Free allocated memory
    free(biomeIds);
    free(rgb);
}

void *generateTileThread(void *arg) {
    TileParams *params = (TileParams *)arg;
    generateTile(params);
    free(params); // Free the allocated parameter structure
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <seed> <tileX> <tileY> <zoomLevel> <scale>\n", argv[0]);
        return 1;
    }

    // Parse input arguments
    uint64_t seed = strtoull(argv[1], NULL, 10);
    int tileX = atoi(argv[2]);
    int tileY = atoi(argv[3]);
    int zoomLevel = atoi(argv[4]);
    int scale = atoi(argv[5]);

    // Set the base tile size (e.g., 128 or dynamic based on zoom level)
    int tileSize = 96;  // This can be adjusted based on zoom level or as needed

    char outputDir[2048];
    snprintf(outputDir, sizeof(outputDir), "/var/www/staging/gme-backend/storage/app/public/tiles");

    if (createDir(outputDir) != 0) {
        return 1;
    }

    Generator g;
    
    // Create a thread for tile generation
    pthread_t thread;
    TileParams *params = malloc(sizeof(TileParams)); // Allocate memory for parameters
    if (!params) {
        fprintf(stderr, "Error allocating memory for parameters\n");
        return 1;
    }

    *params = (TileParams){ &g, seed, tileX, tileY, tileSize, outputDir, zoomLevel, scale };

    // Create a thread for tile generation
    if (pthread_create(&thread, NULL, generateTileThread, params) != 0) {
        fprintf(stderr, "Error creating thread\n");
        free(params);
        return 1;
    }

    // Wait for the thread to finish
    pthread_join(thread, NULL);
    
    printf("Tile generated successfully.\n");
    return 0;
}
