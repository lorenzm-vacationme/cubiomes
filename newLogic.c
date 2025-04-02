#include "generator.h"
#include "util.h"
#include "image_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

#define MAX_PATH_LENGTH 512
#define DEFAULT_TILE_SIZE 16
#define PIXELS_PER_CELL 4
#define CUBIOMES_SCALE 4
#define DIR_PERMISSIONS 0777
#define NUM_THREADS 2

// Lookup table for zoom levels to tile sizes
static const int ZOOM_TILE_SIZES[] = {256, 128, 64, 32, 16, 16};
#define NUM_ZOOM_LEVELS (sizeof(ZOOM_TILE_SIZES) / sizeof(ZOOM_TILE_SIZES[0]))

static int getTileSize(int zoom) {
    if (zoom >= 0 && zoom < NUM_ZOOM_LEVELS) {
        return ZOOM_TILE_SIZES[zoom];
    }
    return DEFAULT_TILE_SIZE;
}

static int createSingleDirectory(const char *path) {
    if (mkdir(path, DIR_PERMISSIONS) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error creating directory %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int createDirectories(const char *path) {
    char tmp[MAX_PATH_LENGTH];
    char *p;
    
    if (strlen(path) >= MAX_PATH_LENGTH) {
        fprintf(stderr, "Path too long\n");
        return -1;
    }
    
    strncpy(tmp, path, MAX_PATH_LENGTH - 1);
    tmp[MAX_PATH_LENGTH - 1] = '\0';
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (createSingleDirectory(tmp) < 0) return -1;
            *p = '/';
        }
    }
    
    return createSingleDirectory(tmp);
}

typedef struct {
    unsigned char *rgb;
    const int *biomeIds;
    const unsigned char (*biomeColors)[3];
    int startRow;
    int endRow;
    int width;
    int height;
    int pixelsPerCell;
} ThreadData;

static void *processChunk(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    const int width = data->width;
    const int pixelsPerCell = data->pixelsPerCell;
    const int scaledWidth = width / pixelsPerCell;
    
    for (int y = data->startRow; y < data->endRow; y++) {
        const int scaledY = y / pixelsPerCell;
        const int rowOffset = y * width;
        const int scaledRowOffset = scaledY * scaledWidth;
        
        for (int x = 0; x < width; x++) {
            const int scaledX = x / pixelsPerCell;
            const int biomeIdx = scaledRowOffset + scaledX;
            const int pixelIdx = (rowOffset + x) * 3;
            
            memcpy(&data->rgb[pixelIdx], 
                   data->biomeColors[data->biomeIds[biomeIdx]], 
                   3);
        }
    }
    
    return NULL;
}

static void parallelBiomesToImage(unsigned char *rgb, 
                                const unsigned char biomeColors[][3], 
                                const int *biomeIds, 
                                int width, int height, 
                                int pixelsPerCell) {
    pthread_t threads[NUM_THREADS];
    ThreadData threadData[NUM_THREADS];
    
    int rowsPerThread = height / NUM_THREADS;
    int remainingRows = height % NUM_THREADS;
    
    int currentRow = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        threadData[i].rgb = rgb;
        threadData[i].biomeIds = biomeIds;
        threadData[i].biomeColors = biomeColors;
        threadData[i].width = width;
        threadData[i].height = height;
        threadData[i].pixelsPerCell = pixelsPerCell;
        threadData[i].startRow = currentRow;
        
        threadData[i].endRow = currentRow + rowsPerThread;
        if (i < remainingRows) {
            threadData[i].endRow++;
        }
        currentRow = threadData[i].endRow;
        
        pthread_create(&threads[i], NULL, processChunk, &threadData[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
}

static int createOutputPath(char *outputPath, size_t maxLen, int64_t seed, int zoom, int x, int z) {
    char basePath[] = "/var/www/storage/app/public/tiles";
    char tmpPath[MAX_PATH_LENGTH];

    int dirLen = snprintf(tmpPath, sizeof(tmpPath), "%s/%ld/%d/%d", basePath, seed, zoom, x);
    if (dirLen < 0 || dirLen >= sizeof(tmpPath)) return -1;

    if (createDirectories(tmpPath) < 0) return -1;

    int fullLen = snprintf(outputPath, maxLen, "%s/%d.png", tmpPath, z);
    if (fullLen < 0 || fullLen >= maxLen) return -1;

    return 0;
}

int64_t parseSeed(const char *str) {
    char *endptr;
    errno = 0; // Reset error number
    
    // Use strtoll which has better error handling than atoll
    int64_t result = strtoll(str, &endptr, 10);
    
    // Check for conversion errors
    if (errno == ERANGE) {
        fprintf(stderr, "Seed value out of range for int64_t: %s\n", str);
        exit(1);
    }
    
    // Check if entire string was consumed
    if (*endptr != '\0') {
        fprintf(stderr, "Invalid characters in seed: %s\n", str);
        exit(1);
    }
    
    return result;
}


int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <seed> <zoom> <x> <z>\n", argv[0]);
        return 1;
    }

    int64_t seed = parseSeed(argv[1]);
    int zoom = atoi(argv[2]);
    int x = atoi(argv[3]);
    int z = atoi(argv[4]);

    printf("Seed: %" PRId64 ", Zoom: %d, X: %d, Z: %d\n", seed, zoom, x, z);

    Generator g;
    setupGenerator(&g, MC_1_20, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    int tileSize = getTileSize(zoom);
    printf("Tile Size: %d\n", tileSize);

    int imgWidth = tileSize * PIXELS_PER_CELL;
    int imgHeight = tileSize * PIXELS_PER_CELL;

    Range r = {
        .scale = CUBIOMES_SCALE,
        .x = x * tileSize,
        .z = z * tileSize,
        .sx = tileSize,
        .sz = tileSize,
        .y = 15,
        .sy = 1
    };

    printf("Range X: %d, Z: %d, SX: %d, SZ: %d\n", r.x, r.z, r.sx, r.sz);

    int *biomeIds = malloc(sizeof(int) * r.sx * r.sz);
    if (!biomeIds) {
        fprintf(stderr, "Failed to allocate biome cache\n");
        return 1;
    }

    unsigned char *rgb = malloc(3 * imgWidth * imgHeight);
    if (!rgb) {
        fprintf(stderr, "Failed to allocate RGB buffer\n");
        free(biomeIds);
        return 1;
    }

    genBiomes(&g, biomeIds, r);

    unsigned char biomeColors[256][3];
    initBiomeColors(biomeColors);
    parallelBiomesToImage(rgb, biomeColors, biomeIds, imgWidth, imgHeight, PIXELS_PER_CELL);

    char outputPath[MAX_PATH_LENGTH];
    if (createOutputPath(outputPath, sizeof(outputPath), seed, zoom, x, z) < 0) {
        fprintf(stderr, "Failed to create output path\n");
        free(biomeIds);
        free(rgb);
        return 1;
    }

    if (savePNG(outputPath, rgb, imgWidth, imgHeight) != 0) {
        fprintf(stderr, "Error saving image %s\n", outputPath);
        free(biomeIds);
        free(rgb);
        return 1;
    }

    printf("Saved: %s\n", outputPath);
    free(biomeIds);
    free(rgb);
    return 0;
}
