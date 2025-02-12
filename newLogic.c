#include "generator.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include "image_utils.h"
#include <pthread.h>
#include <unistd.h>

// Function to create directories as needed
int createDir(const char *path) {
    char tmp[2048];
    char *p = tmp;
    snprintf(tmp, sizeof(tmp), "%s", path);

    if (tmp[0] == '/') {
        p = tmp + 1;
    } else {
        p = tmp;
    }

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) && errno != EEXIST) {
                fprintf(stderr, "Error creating directory %s: %s\n", tmp, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0777) && errno != EEXIST) {
        fprintf(stderr, "Error creating directory %s: %s\n", tmp, strerror(errno));
        return -1;
    }

    return 0;
}

// Structure to hold parameters for tile generation
typedef struct {
    Generator *g;
    int seed;
    int zoom;
    int x;
    int z;
    int tileSize;
    const char *outputDir;
} TileParams;

void generateTile(TileParams *params) {
    Generator *g = params->g;
    int seed = params->seed;
    int x = params->x;
    int z = params->z;
    int tileSize = params->tileSize;
    const char *outputDir = params->outputDir;
    int zoom = params->zoom;
    
    int pix4cell = 4;
    int scale = 4;
    
    int worldX = x * tileSize;
    int worldZ = z * tileSize;

    Range r = {
        .scale = scale,
        .x = worldX,
        .z = worldZ,
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

    int imgWidth = tileSize * pix4cell;
    int imgHeight = tileSize * pix4cell;

    unsigned char *rgb = (unsigned char *)malloc(3 * imgWidth * imgHeight);
    if (!rgb) {
        fprintf(stderr, "Error allocating memory for image\n");
        free(biomeIds);
        return;
    }

    unsigned char biomeColors[256][3];
    initBiomeColors(biomeColors);

    biomesToImage(rgb, biomeColors, biomeIds, r.sx, r.sz, pix4cell, 2);

    char tileDir[2048], outputFile[4096];
    snprintf(tileDir, sizeof(tileDir), "%s/%d/%d/%d", outputDir, seed, zoom, x);
    snprintf(outputFile, sizeof(outputFile), "%s/%d.png", tileDir, z);

    if (createDir(tileDir) != 0 || savePNG(outputFile, rgb, imgWidth, imgHeight) != 0) {
        fprintf(stderr, "Error saving image file for tile %d_%d at zoom level %d\n", x, z, zoom);
    } else {
        printf("Tile %d_%d at zoom level %d generated and saved to %s\n", x, z, zoom, outputFile);
    }

    free(biomeIds);
    free(rgb);
}

void *generateTileThread(void *arg) {
    TileParams *params = (TileParams *)arg;
    generateTile(params);
    free(params);
    return NULL;
}

int getTileSize(int zoom) {
    switch(zoom) {
        case 0: return 256;
        case 1: return 128;
        case 2: return 64;
        case 3: return 32;
        case 4: return 16;
        default: return 16;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: ./generate_map <seed> <zoom> <x> <z>\n");
        return 1;
    }

    int seed = atoi(argv[1]);
    int zoom = atoi(argv[2]);
    int x = atoi(argv[3]);
    int z = atoi(argv[4]);
    
    int tileSize = getTileSize(zoom);
    
    char outputDir[2048];
    snprintf(outputDir, sizeof(outputDir), "/var/www/production/gme-backend/storage/app/public/tiles");

    if (createDir(outputDir) != 0) {
        return 1;
    }

    Generator g;
    setupGenerator(&g, MC_1_20, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    // Create thread for tile generation
    pthread_t thread;
    TileParams *params = malloc(sizeof(TileParams));
    if (!params) {
        fprintf(stderr, "Error allocating memory for parameters\n");
        return 1;
    }

    *params = (TileParams){
        .g = &g,
        .seed = seed,
        .zoom = zoom,
        .x = x,
        .z = z,
        .tileSize = tileSize,
        .outputDir = outputDir
    };

    if (pthread_create(&thread, NULL, generateTileThread, params) != 0) {
        fprintf(stderr, "Error creating thread\n");
        free(params);
        return 1;
    }

    pthread_join(thread, NULL);
    
    printf("Tile generated successfully.\n");
    return 0;
}