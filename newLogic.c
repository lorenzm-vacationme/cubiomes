#include "generator.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include "image_utils.h"

// Function to create directories recursively
void createDirectories(const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

int main(int argc, char *argv[])
{
    if (argc < 5) {
        printf("Usage: ./generate_map <seed> <zoom> <x> <z>\n");
        return 1;
    }

    int seed = atoi(argv[1]);
    int zoom = atoi(argv[2]);
    int x = atoi(argv[3]);
    int z = atoi(argv[4]);

    Generator g;
    setupGenerator(&g, MC_1_20, 0);

    uint64_t worldSeed = (uint64_t) seed;
    applySeed(&g, DIM_OVERWORLD, worldSeed);
    
    //  0, 1,  2,  3,   4,   5, 
    // 16, 32, 64, 128, 256, 512
    int tileSize;

    if (zoom == 0) {
        tileSize = 256;
    } else if (zoom == 1) {
        tileSize = 128;
    } else if (zoom == 2) {
        tileSize = 64;
    } else if (zoom == 3) {
        tileSize = 32;
    } else if (zoom == 4) {
        tileSize = 16;
    } else {
        tileSize = 16;  // Default to 256 if zoom is out of range
    }
    

     // Standard OpenLayers tile size
    int pix4cell = 4;   // Pixels per biome cell
    int scale = 4;      // Cubiomes scale factor (1:16)

    // int worldX = x * tileSize / pix4cell * scale;
    // int worldZ = z * tileSize / pix4cell * scale;
    int worldX = x * tileSize;
    int worldZ = z * tileSize;

    // Range r;
    // r.scale = scale;
    // r.x = worldX;
    // r.z = worldZ;
    // r.sx = tileSize / pix4cell;
    // r.sz = tileSize / pix4cell;
    // r.y = 256;
    // r.sy = 1;

    Range r = {
        .scale = scale,
        .x = worldX,
        .z = worldZ,
        .sx = tileSize,
        .sz = tileSize,
        .y = 15,
        .sy = 1
    };

    int *biomeIds = allocCache(&g, r);
    genBiomes(&g, biomeIds, r);

    int imgWidth = tileSize * pix4cell , imgHeight = tileSize * pix4cell;

    unsigned char biomeColors[256][3];
    initBiomeColors(biomeColors);
    unsigned char *rgb = (unsigned char *) malloc(3 * imgWidth * imgHeight);

    biomesToImage(rgb, biomeColors, biomeIds, r.sx, r.sz, pix4cell, 2);

    // Define output directory with seed first
    char outputDir[200];
    snprintf(outputDir, sizeof(outputDir), "/var/www/production/gme-backend/storage/app/public/tiles/%d/%d/%d", seed, zoom, x);
    // snprintf(outputDir, sizeof(outputDir), "/var/www/production/gme-backend/storage/app/public/tiles");
 
    // Create directories recursively
    createDirectories(outputDir);

    // Save file with updated path
    char filename[250];
    snprintf(filename, sizeof(filename), "%s/%d.png", outputDir, z);
    
    if (savePNG(filename, rgb, imgWidth, imgHeight) != 0) {
        printf("Error saving image: %s (errno: %d - %s)\n", filename, errno, strerror(errno));
    } else {
        printf("Saved: %s\n", filename);
    }

    free(biomeIds);
    free(rgb);

    return 0;
}
