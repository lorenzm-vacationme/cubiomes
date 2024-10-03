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
#include <time.h>
#include <pthread.h>
#include <dirent.h>

int totalTiles = 0;
int completedTiles = 0;
time_t startTime;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

// Define thread pool size
#define MAX_THREADS 4

// Thread pool structure
typedef struct {
    pthread_t threads[MAX_THREADS];
    int busy[MAX_THREADS];
    pthread_mutex_t lock;
} ThreadPool;

// Define the batch size
#define BATCH_SIZE 100

ThreadPool threadPool;

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

// Check if the tile already exists to prevent regeneration
int tileExists(const char *outputFile) {
    struct stat buffer;
    return (stat(outputFile, &buffer) == 0);
}

// Function to generate a single tile
void generateTile(Generator *g, uint64_t seed, int tileX, int tileY, int tileSize, const char *outputDir, int zoomLevel, int scale) {
    char outputFile[8192];
    snprintf(outputFile, sizeof(outputFile), "%s/%lu/%d/%d/%d.png", outputDir, seed, zoomLevel, tileX, tileY);

    // Check if tile already exists (cache check)
    if (tileExists(outputFile)) {
        pthread_mutex_lock(&mutex);
        completedTiles++;
        pthread_mutex_unlock(&mutex);
        return;
    }

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
        free(biomeIds);
        return;
    }

    unsigned char biomeColors[256][3];
    initBiomeColors(biomeColors);

    biomesToImage(rgb, biomeColors, biomeIds, r.sx, r.sz, pix4cell, 2);

    char tileDir[4096];
    snprintf(tileDir, sizeof(tileDir), "%s/%lu/%d/%d", outputDir, seed, zoomLevel, tileX);

    if (createDir(tileDir) != 0 || savePNG(outputFile, rgb, imgWidth, imgHeight) != 0) {
        fprintf(stderr, "Error saving image file for tile %d_%d at zoom level %d\n", tileX, tileY, zoomLevel);
    } else {
        pthread_mutex_lock(&mutex);
        completedTiles++;
        double elapsedSeconds = difftime(time(NULL), startTime);
        double estimatedTotalTime = (elapsedSeconds / completedTiles) * totalTiles;
        printf("Tile %d of %d generated and saved to %s\nEstimated time remaining: %.2f seconds\n",
               completedTiles, totalTiles, outputFile, estimatedTotalTime - elapsedSeconds);
        pthread_mutex_unlock(&mutex);
    }

    free(biomeIds);
    free(rgb);
}

typedef struct {
    uint64_t seed;
    const char *outputDir;
    int zoomLevel;
    int scale;
    int base_tile_size;
    int tile_count;
} ZoomLevelParams;

// Thread worker function
void *worker(void *arg) {
    ZoomLevelParams *params = (ZoomLevelParams *)arg;
    Generator g;
    int tileSize = params->base_tile_size;
    int x = params->tile_count / 2;
    int y = x;
    int dx = 0, dy = -1;
    int segmentLength = 1, segmentPassed = 0, turnsMade = 0;

    for (int i = 0; i < params->tile_count * params->tile_count; ++i) {
        if (x >= 0 && x < params->tile_count && y >= 0 && y < params->tile_count) {
            generateTile(&g, params->seed, x, y, tileSize, params->outputDir, params->zoomLevel, params->scale);
        }

        x += dx;
        y += dy;
        segmentPassed++;
        if (segmentPassed == segmentLength) {
            int temp = dx;
            dx = -dy;
            dy = temp;
            segmentPassed = 0;
            if (++turnsMade % 2 == 0) segmentLength++;
        }

        if ((i + 1) % BATCH_SIZE == 0) {
            pthread_mutex_lock(&mutex);
            printf("Processed batch of %d tiles for zoom level %d\n", BATCH_SIZE, params->zoomLevel);
            pthread_mutex_unlock(&mutex);
        }
    }

    pthread_exit(NULL);
}

// Initialize the thread pool
void initThreadPool(ThreadPool *pool) {
    pthread_mutex_init(&pool->lock, NULL);
    for (int i = 0; i < MAX_THREADS; i++) {
        pool->busy[i] = 0;
    }
}

// Assign a job to an available thread
void assignJob(ThreadPool *pool, ZoomLevelParams *params) {
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < MAX_THREADS; i++) {
        if (!pool->busy[i]) {
            pthread_create(&pool->threads[i], NULL, worker, params);
            pool->busy[i] = 1;
            break;
        }
    }
    pthread_mutex_unlock(&pool->lock);
}

// Wait for all threads to finish
void waitForThreads(ThreadPool *pool) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (pool->busy[i]) {
            pthread_join(pool->threads[i], NULL);
            pool->busy[i] = 0;
        }
    }
}

void generateTilesForZoomLevels(uint64_t seed, const char *outputDir) {
    ZoomLevelParams zoomLevels[] = {
        {seed, outputDir, 3, 96, 128, 8},
        {seed, outputDir, 4, 48, 128, 16},
        {seed, outputDir, 5, 24, 128, 32},
        {seed, outputDir, 6, 12, 128, 32},
    };
    int numZoomLevels = sizeof(zoomLevels) / sizeof(zoomLevels[0]);

    totalTiles = 0;
    for (int i = 0; i < numZoomLevels; i++) {
        totalTiles += zoomLevels[i].tile_count * zoomLevels[i].tile_count;
    }

    // Initialize the thread pool
    initThreadPool(&threadPool);

    // Assign jobs to threads
    for (int i = 0; i < numZoomLevels; i++) {
        assignJob(&threadPool, &zoomLevels[i]);
    }

    // Wait for all threads to finish
    waitForThreads(&threadPool);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <seed>\n", argv[0]);
        return 1;
    }

    uint64_t seed = strtoull(argv[1], NULL, 10);
    startTime = time(NULL);

    char outputDir[2048];
    snprintf(outputDir, sizeof(outputDir), "/var/www/production/gme-backend/storage/app/public/tiles");

    // Generate tiles for multiple zoom levels
    generateTilesForZoomLevels(seed, outputDir);

    return 0;
}
