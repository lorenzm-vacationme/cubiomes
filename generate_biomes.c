#include <stdio.h>   
#include <stdlib.h> 
#include <string.h>
#include <sys/stat.h>  // For mkdir()
#include <sys/types.h>
#include <unistd.h>    // For access()
#include "generator.h"
#include "util.h"
#include "image_utils.h"

// Function to create directory if it doesn't exist
void ensureDirectoryExists(const char *dirPath) {
    struct stat st = {0};

    if (stat(dirPath, &st) == -1) {
        if (mkdir(dirPath, 0755) == -1) {
            perror("Failed to create directory");
            exit(1);
        }
        printf("Directory created: %s\n", dirPath);
    }
}

// Function to check if a file exists
int fileExists(const char *filePath) {
    return access(filePath, F_OK) == 0;  // Returns 0 if file exists, -1 if not
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s <path> <seed>\n", argv[0]);
        return 1;
    }

    char *path = argv[1];
    int64_t seed = strtoll(argv[2], NULL, 10);  // Convert seed string to int64_t
    char *basePath;

    if (strcmp(path, "production") == 0) {
        basePath = "/var/www/production/gme-backend/storage/app/public/images/2d-map";
    } else if (strcmp(path, "staging") == 0) {
        basePath = "/var/www/staging/gme-backend/storage/app/public/images/2d-map";
    } else {
        basePath = "/var/www/storage/app/public/images/2d-map";
    }

    ensureDirectoryExists(basePath);  // Ensure directory exists

    char filePath[512];
    snprintf(filePath, sizeof(filePath), "%s/2d-map_%ld.png", basePath, seed);

    // Check if the image file already exists
    if (fileExists(filePath)) {
        printf("Image already exists: %s\n", filePath);
        return 0;  // Skip generation if file is found
    }

    printf("Generating and saving map to: %s\n", filePath);

    Generator g;
    setupGenerator(&g, MC_1_20, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    Range r;
    r.scale = 4;
    r.x = 0, r.z = 0;
    r.sx = 400, r.sz = 400;
    r.y = 15, r.sy = 1;

    int *biomeIds = allocCache(&g, r);
    genBiomes(&g, biomeIds, r);

    int pix4cell = 16;
    int imgWidth = pix4cell * r.sx, imgHeight = pix4cell * r.sz;
    unsigned char biomeColors[256][3];
    initBiomeColors(biomeColors);
    unsigned char *rgb = (unsigned char *)malloc(3 * imgWidth * imgHeight);
    biomesToImage(rgb, biomeColors, biomeIds, r.sx, r.sz, pix4cell, 2);

    // Save as PNG
    savePNG(filePath, rgb, imgWidth, imgHeight);

    free(biomeIds);
    free(rgb);

    return 0;
}
