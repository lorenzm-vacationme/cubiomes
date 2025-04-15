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
#define NUM_THREADS 3
#define MIN_ZOOM 0
#define MAX_ZOOM 4

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

// int createOutputPath(char *outputPath, size_t maxLen, uint64_t seed, int zoom, int x, int z) {
//     char basePath[] = "/var/www/storage/app/public/tiles/";
//     char tmpPath[MAX_PATH_LENGTH];

//     int dirLen = snprintf(tmpPath, sizeof(tmpPath), "%s/%" PRIu64 "/%d/%d", basePath, seed, zoom, x);
//     if (dirLen < 0 || dirLen >= sizeof(tmpPath)) {
//         fprintf(stderr, "Path snprintf failed\n");
//         return -1;
//     }

//     printf("Creating directory: %s\n", tmpPath);  // Debugging output
//     if (createDirectories(tmpPath) < 0) {
//         fprintf(stderr, "Failed to create directories: %s\n", tmpPath);
//         return -1;
//     }

//     int fullLen = snprintf(outputPath, maxLen, "%s/%d.ppm", tmpPath, z);
//     if (fullLen < 0 || fullLen >= maxLen) {
//         fprintf(stderr, "Output path snprintf failed\n");
//         return -1;
//     }

//     printf("Final output path: %s\n", outputPath);  // Debugging output
//     return 0;
// }

int createOutputPath(char *outputPath, size_t maxLen, const char *seedStr, int zoom, int x, int z) {
    char basePath[] = "/var/www/production/gme-backend/storage/app/public/tiles";
    // char basePath[] = "/var/www/storage/app/public/tiles/";
    char tmpPath[MAX_PATH_LENGTH];

    // Use the original seed string in the path
    int dirLen = snprintf(tmpPath, sizeof(tmpPath), "%s/%s/%d/%d", basePath, seedStr, zoom, x);
    if (dirLen < 0 || dirLen >= sizeof(tmpPath)) {
        fprintf(stderr, "Path snprintf failed\n");
        return -1;
    }

    printf("Creating directory: %s\n", tmpPath);
    if (createDirectories(tmpPath) < 0) {
        fprintf(stderr, "Failed to create directories: %s\n", tmpPath);
        return -1;
    }

    int fullLen = snprintf(outputPath, maxLen, "%s/%d.ppm", tmpPath, z);
    if (fullLen < 0 || fullLen >= maxLen) {
        fprintf(stderr, "Output path snprintf failed\n");
        return -1;
    }

    printf("Final output path: %s\n", outputPath);
    return 0;
}

// uint64_t parseSeed(const char *str) {
//     // Fast path: try a quick hash for very large values
//     size_t len = strlen(str);
//     if (len > 32) {  // A uint64_t can hold at most 20 digits
//         uint64_t hash = 5381;
//         const char *ptr = str;
//         int c;

//         while ((c = *ptr++)) {
//             hash = ((hash << 5) + hash) + c; // hash * 33 + c
//         }

//         fprintf(stderr, "Large seed detected, using hash: %" PRIu64 "\n", hash);
//         return hash;
//     }

//     // Standard path for smaller values
//     char *endptr;
//     errno = 0;
//     unsigned long long ull_result = strtoull(str, &endptr, 10);

//     if (errno == ERANGE || *endptr != '\0') {
//         uint64_t hash = 5381;
//         const char *ptr = str;
//         int c;

//         while ((c = *ptr++)) {
//             hash = ((hash << 5) + hash) + c;
//         }

//         fprintf(stderr, "Invalid seed format, using hash: %" PRIu64 "\n", hash);
//         return hash;
//     }

//     return (uint64_t)ull_result;
// }

uint64_t parseSeed(const char *str) {
    // Try direct conversion first, regardless of length
    char *endptr;
    errno = 0;
    unsigned long long ull_result = strtoull(str, &endptr, 10);

    // If the conversion was successful and complete
    if (errno != ERANGE && *endptr == '\0') {
        return (uint64_t)ull_result;
    }

    // If the number is too large for uint64_t or invalid format,
    // we could use a more advanced handling method here.
    // For now, we'll use the original, which might truncate large values
    // but will preserve more of the structure than a hash.
    fprintf(stderr, "Warning: Seed is too large or invalid format, using numeric portion only: %llu\n", ull_result);
    return (uint64_t)ull_result;
}

// Function to generate a single tile at the specified zoom level
// int generateTile(Generator *g, uint64_t seed, int zoom, int x, int z) {
//     int tileSize = getTileSize(zoom);
//     printf("Generating tile at zoom %d (size: %d) for x=%d, z=%d\n", zoom, tileSize, x, z);

//     int imgWidth = tileSize * PIXELS_PER_CELL;
//     int imgHeight = tileSize * PIXELS_PER_CELL;

//     Range r = {
//         .scale = CUBIOMES_SCALE,
//         .x = x * tileSize,
//         .z = z * tileSize,
//         .sx = tileSize,
//         .sz = tileSize,
//         .y = 15,
//         .sy = 1
//     };

//     printf("Range X: %d, Z: %d, SX: %d, SZ: %d\n", r.x, r.z, r.sx, r.sz);

//     int *biomeIds = malloc(sizeof(int) * r.sx * r.sz);
//     if (!biomeIds) {
//         fprintf(stderr, "Failed to allocate biome cache\n");
//         return 1;
//     }

//     unsigned char *rgb = malloc(3 * imgWidth * imgHeight);
//     if (!rgb) {
//         fprintf(stderr, "Failed to allocate RGB buffer\n");
//         free(biomeIds);
//         return 1;
//     }

//     genBiomes(g, biomeIds, r);

//     unsigned char biomeColors[256][3];
//     initBiomeColors(biomeColors);
//     parallelBiomesToImage(rgb, biomeColors, biomeIds, imgWidth, imgHeight, PIXELS_PER_CELL);

//     char outputPath[MAX_PATH_LENGTH];
//     if (createOutputPath(outputPath, sizeof(outputPath), seed, zoom, x, z) < 0) {
//         fprintf(stderr, "Failed to create output path\n");
//         free(biomeIds);
//         free(rgb);
//         return 1;
//     }

//     if (savePPM(outputPath, rgb, imgWidth, imgHeight) != 0) {
//         fprintf(stderr, "Error saving PPM file\n");
//         free(biomeIds);
//         free(rgb);
//         return 1;
//     }

//     printf("Saved: %s\n", outputPath);
//     free(biomeIds);
//     free(rgb);
//     return 0;
// }

// int generateTile(Generator *g, uint64_t seed, int zoom, int x, int z) {
    // int tileSize = getTileSize(zoom);
    // printf("Generating tile at zoom %d (size: %d) for x=%d, z=%d\n", zoom, tileSize, x, z);

    int generateTile(Generator *g, uint64_t seed, const char *seedStr, int zoom, int x, int z) {
        int tileSize = getTileSize(zoom);
        printf("Generating tile at zoom %d (size: %d) for x=%d, z=%d\n", zoom, tileSize, x, z);
    
        // Original biome calculation dimensions
        Range r = {
            .scale = CUBIOMES_SCALE,
            .x = x * tileSize,
            .z = z * tileSize,
            .sx = tileSize,
            .sz = tileSize,
            .y = 15,
            .sy = 1
        };
    
        const int outputWidth = 64;
        const int outputHeight = 64;
        
        // Calculate scaling factors
        const float widthScale = (float)outputWidth / (tileSize * PIXELS_PER_CELL);
        const float heightScale = (float)outputHeight / (tileSize * PIXELS_PER_CELL);
    
        printf("Range X: %d, Z: %d, SX: %d, SZ: %d\n", r.x, r.z, r.sx, r.sz);
    
        int *biomeIds = malloc(sizeof(int) * r.sx * r.sz);
        if (!biomeIds) {
            fprintf(stderr, "Failed to allocate biome cache\n");
            return 1;
        }
    
        // Temporary buffer for full resolution image
        unsigned char *fullRgb = malloc(3 * tileSize * PIXELS_PER_CELL * tileSize * PIXELS_PER_CELL);
        if (!fullRgb) {
            fprintf(stderr, "Failed to allocate RGB buffer\n");
            free(biomeIds);
            return 1;
        }
    
        // Final output buffer
        unsigned char *smallRgb = malloc(3 * outputWidth * outputHeight);
        if (!smallRgb) {
            fprintf(stderr, "Failed to allocate small RGB buffer\n");
            free(biomeIds);
            free(fullRgb);
            return 1;
        }
    
        genBiomes(g, biomeIds, r);
    
        unsigned char biomeColors[256][3];
        initBiomeColors(biomeColors);
        
        // Generate at full resolution first
        parallelBiomesToImage(fullRgb, biomeColors, biomeIds, 
                            tileSize * PIXELS_PER_CELL, 
                            tileSize * PIXELS_PER_CELL, 
                            PIXELS_PER_CELL);
    
        // Scale down to output dimensions
        for (int y = 0; y < outputHeight; y++) {
            for (int x = 0; x < outputWidth; x++) {
                // Calculate corresponding position in full resolution image
                int srcX = (int)(x / widthScale);
                int srcY = (int)(y / heightScale);
                
                // Copy pixel
                memcpy(&smallRgb[(y * outputWidth + x) * 3],
                      &fullRgb[(srcY * tileSize * PIXELS_PER_CELL + srcX) * 3],
                      3);
            }
        }
    
        char outputPath[MAX_PATH_LENGTH];
        if (createOutputPath(outputPath, sizeof(outputPath), seedStr, zoom, x, z) < 0) {
            fprintf(stderr, "Failed to create output path\n");
            free(biomeIds);
            free(fullRgb);
            free(smallRgb);
            return 1;
        }
    
        if (savePPM(outputPath, smallRgb, outputWidth, outputHeight) != 0) {
            fprintf(stderr, "Error saving PPM file\n");
            free(biomeIds);
            free(fullRgb);
            free(smallRgb);
            return 1;
        }
    
        printf("Saved: %s\n", outputPath);
        free(biomeIds);
        free(fullRgb);
        free(smallRgb);
        return 0;
    }

void showUsage(const char *program) {
    fprintf(stderr, "Usage: %s <seed> <zoom> <x> <z>\n", program);
    fprintf(stderr, "  Generates tiles for all zoom levels (0-%d) using the same x,z coordinates\n", MAX_ZOOM);
}

// int main(int argc, char *argv[]) {
//     if (argc < 5) {
//         showUsage(argv[0]);
//         return 1;
//     }

//     uint64_t seed = parseSeed(argv[1]);
//     int baseZoom = atoi(argv[2]);
//     int x = atoi(argv[3]);
//     int z = atoi(argv[4]);
int main(int argc, char *argv[]) {
    if (argc < 5) {
        showUsage(argv[0]);
        return 1;
    }

    const char *seedStr = argv[1];
    uint64_t seed = parseSeed(seedStr);

    int baseZoom = atoi(argv[2]);
    int x = atoi(argv[3]);
    int z = atoi(argv[4]);

    if (baseZoom < MIN_ZOOM || baseZoom > MAX_ZOOM) {
        fprintf(stderr, "Zoom level must be between %d and %d\n", MIN_ZOOM, MAX_ZOOM);
        return 1;
    }

    printf("Seed: %" PRIu64 ", Base Zoom: %d, X: %d, Z: %d\n", seed, baseZoom, x, z);
    printf("Generating tiles for all zoom levels (0-%d) with same coordinates...\n", MAX_ZOOM);

//     Generator g;
//     setupGenerator(&g, MC_1_20, 0);
//     applySeed(&g, DIM_OVERWORLD, seed);

//     // First generate the base zoom level tile
//     if (generateTile(&g, seed, baseZoom, x, z) != 0) {
//         fprintf(stderr, "Failed to generate base tile\n");
//         return 1;
//     }

//     // Then generate tiles for all other zoom levels using the same x,z
//     for (int zoom = MIN_ZOOM; zoom <= MAX_ZOOM; zoom++) {
//         // Skip the base zoom level as we already generated it
//         if (zoom == baseZoom) continue;
        
//         // Generate the tile for this zoom level using the same x,z coordinates
//         if (generateTile(&g, seed, zoom, x, z) != 0) {
//             fprintf(stderr, "Failed to generate tile for zoom level %d\n", zoom);
//             // Continue with other zoom levels
//         }
//     }

//     printf("All tiles generated successfully.\n");
//     return 0;
// }

    Generator g;
    setupGenerator(&g, MC_1_20, 0);
    applySeed(&g, DIM_OVERWORLD, seed); // Use parsed seed for generation

    // Pass both parsed seed (for biome gen) and original string (for path)
    if (generateTile(&g, seed, seedStr, baseZoom, x, z) != 0) {
        fprintf(stderr, "Failed to generate base tile\n");
        return 1;
    }

    // Generate other zoom levels
    for (int zoom = MIN_ZOOM; zoom <= MAX_ZOOM; zoom++) {
        if (zoom == baseZoom) continue;
        generateTile(&g, seed, seedStr, zoom, x, z);
    }

    printf("All tiles generated successfully.\n");
    return 0;
}
