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
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>

#define MAX_PATH_LENGTH 512
#define DEFAULT_TILE_SIZE 16
#define PIXELS_PER_CELL 4
#define CUBIOMES_SCALE 4
#define DIR_PERMISSIONS 0777
#define NUM_THREADS 4
#define CACHE_SIZE 100
#define MAX_SEED_LENGTH 19 // Maximum digits for int64_t (including sign)

// Lookup table for zoom levels to tile sizes
static const int ZOOM_TILE_SIZES[] = {256, 128, 64, 32, 16, 16};
#define NUM_ZOOM_LEVELS (sizeof(ZOOM_TILE_SIZES) / sizeof(ZOOM_TILE_SIZES[0]))

// Memory buffers
static int *biomeCache = NULL;
static unsigned char *rgbBuffer = NULL;
static unsigned char biomeColors[256][3];

// Thread pool and synchronization
static pthread_t threadPool[NUM_THREADS];
static bool threadsRunning = false;
static pthread_mutex_t workMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t workCond = PTHREAD_COND_INITIALIZER;

// Tile cache
typedef struct {
    int64_t seed;
    int zoom;
    int x;
    int z;
    unsigned char* image;
    size_t size;
} TileCache;

static TileCache tileCache[CACHE_SIZE];
static int cacheIndex = 0;

// Work item for thread pool
typedef struct {
    unsigned char *rgb;
    const int *biomeIds;
    const unsigned char (*biomeColors)[3];
    int startRow;
    int endRow;
    int width;
    int height;
    int pixelsPerCell;
    bool done;
} WorkItem;

static WorkItem workItems[NUM_THREADS];

static bool isValidSeed(const char *str) {
    if (str == NULL || *str == '\0') {
        return false;
    }
    
    // Check length (up to 19 digits plus optional sign)
    size_t len = strlen(str);
    if (len > MAX_SEED_LENGTH + 1) { // +1 for possible sign
        return false;
    }
    
    // Check for optional leading sign
    const char *p = str;
    if (*p == '-' || *p == '+') {
        p++;
        // After sign we need at least one digit
        if (*p == '\0') {
            return false;
        }
    }
    
    // Check remaining characters are digits
    for (; *p; p++) {
        if (!isdigit(*p)) {
            return false;
        }
    }
    
    return true;
}

static int getTileSize(int zoom) {
    return (zoom >= 0 && zoom < (int)NUM_ZOOM_LEVELS) ? ZOOM_TILE_SIZES[zoom] : DEFAULT_TILE_SIZE;
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
    char *p = NULL;
    
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

static void* workerThread(void* arg) {
    while (threadsRunning) {
        pthread_mutex_lock(&workMutex);
        
        // Wait for work
        bool hasWork = false;
        int threadId = -1;
        
        for (int i = 0; i < NUM_THREADS; i++) {
            if (!workItems[i].done && workItems[i].rgb != NULL) {
                hasWork = true;
                threadId = i;
                workItems[i].done = true; // Mark as being processed
                break;
            }
        }
        
        if (!hasWork) {
            pthread_cond_wait(&workCond, &workMutex);
            pthread_mutex_unlock(&workMutex);
            continue;
        }
        
        pthread_mutex_unlock(&workMutex);
        
        // Process work
        WorkItem* work = &workItems[threadId];
        const int width = work->width;
        const int pixelsPerCell = work->pixelsPerCell;
        const int scaledWidth = width / pixelsPerCell;
        
        for (int y = work->startRow; y < work->endRow; y++) {
            const int scaledY = y / pixelsPerCell;
            const int rowOffset = y * width;
            const int scaledRowOffset = scaledY * scaledWidth;
            
            for (int x = 0; x < width; x++) {
                const int scaledX = x / pixelsPerCell;
                const int biomeIdx = scaledRowOffset + scaledX;
                const int pixelIdx = (rowOffset + x) * 3;
                
                memcpy(&work->rgb[pixelIdx], 
                      work->biomeColors[work->biomeIds[biomeIdx]], 
                      3);
            }
        }
        
        pthread_mutex_lock(&workMutex);
        work->rgb = NULL; // Mark work as complete
        pthread_mutex_unlock(&workMutex);
    }
    return NULL;
}

static void parallelBiomesToImage(unsigned char *rgb, 
                                const unsigned char biomeColors[][3], 
                                const int *biomeIds, 
                                int width, int height, 
                                int pixelsPerCell) {
    int rowsPerThread = height / NUM_THREADS;
    int remainingRows = height % NUM_THREADS;
    int currentRow = 0;
    
    pthread_mutex_lock(&workMutex);
    
    for (int i = 0; i < NUM_THREADS; i++) {
        workItems[i].rgb = rgb;
        workItems[i].biomeIds = biomeIds;
        workItems[i].biomeColors = biomeColors;
        workItems[i].width = width;
        workItems[i].height = height;
        workItems[i].pixelsPerCell = pixelsPerCell;
        workItems[i].startRow = currentRow;
        workItems[i].endRow = currentRow + rowsPerThread;
        workItems[i].done = false;
        
        if (i < remainingRows) {
            workItems[i].endRow++;
        }
        currentRow = workItems[i].endRow;
    }
    
    pthread_cond_broadcast(&workCond);
    pthread_mutex_unlock(&workMutex);
    
    // Wait for completion
    bool allDone;
    do {
        pthread_mutex_lock(&workMutex);
        allDone = true;
        for (int i = 0; i < NUM_THREADS; i++) {
            if (workItems[i].rgb != NULL) {
                allDone = false;
                break;
            }
        }
        pthread_mutex_unlock(&workMutex);
        
        if (!allDone) {
            usleep(1000); // Sleep 1ms to avoid busy waiting
        }
    } while (!allDone);
}

static unsigned char* checkCache(int64_t seed, int zoom, int x, int z, size_t* size) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (tileCache[i].seed == seed && 
            tileCache[i].zoom == zoom && 
            tileCache[i].x == x && 
            tileCache[i].z == z) {
            *size = tileCache[i].size;
            return tileCache[i].image;
        }
    }
    return NULL;
}

static void addToCache(int64_t seed, int zoom, int x, int z, unsigned char* image, size_t size) {
    // Free old cache entry if it exists
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (tileCache[i].seed == seed && 
            tileCache[i].zoom == zoom && 
            tileCache[i].x == x && 
            tileCache[i].z == z) {
            free(tileCache[i].image);
            tileCache[i].image = image;
            tileCache[i].size = size;
            return;
        }
    }
    
    // Add new entry
    tileCache[cacheIndex].seed = seed;
    tileCache[cacheIndex].zoom = zoom;
    tileCache[cacheIndex].x = x;
    tileCache[cacheIndex].z = z;
    tileCache[cacheIndex].image = image;
    tileCache[cacheIndex].size = size;
    cacheIndex = (cacheIndex + 1) % CACHE_SIZE;
}

static int createOutputPath(char *outputPath, size_t maxLen, int64_t seed, int zoom, int x, int z) {
    char basePath[] = "/var/www/production/gme-backend/storage/app/public/tiles";
    char tmpPath[MAX_PATH_LENGTH];
    
    int dirLen = snprintf(tmpPath, sizeof(tmpPath), "%s/%" PRId64 "/%d/%d", basePath, seed, zoom, x);
    if (dirLen < 0 || dirLen >= sizeof(tmpPath)) {
        return -1;
    }
    
    if (createDirectories(tmpPath) < 0) {
        return -1;
    }
    
    int fullLen = snprintf(outputPath, maxLen, "%s/%d.png", tmpPath, z);
    if (fullLen < 0 || fullLen >= maxLen) {
        return -1;
    }
    
    return 0;
}

static void initialize() {
    // Pre-allocate maximum needed buffers
    size_t maxTileSize = ZOOM_TILE_SIZES[0]; // 256 (largest zoom level)
    size_t maxImgSize = maxTileSize * PIXELS_PER_CELL;
    size_t maxBiomeSize = maxTileSize * maxTileSize * sizeof(int);
    size_t maxRgbSize = 3 * maxImgSize * maxImgSize;
    
    biomeCache = malloc(maxBiomeSize);
    rgbBuffer = malloc(maxRgbSize);
    if (!biomeCache || !rgbBuffer) {
        fprintf(stderr, "Failed to allocate buffers\n");
        exit(1);
    }
    
    // Initialize thread pool
    threadsRunning = true;
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threadPool[i], NULL, workerThread, NULL);
    }
    
    // Initialize biome colors
    initBiomeColors(biomeColors);
    
    // Initialize work items
    for (int i = 0; i < NUM_THREADS; i++) {
        workItems[i].rgb = NULL;
        workItems[i].done = false;
    }
}

static void cleanup() {
    // Signal threads to exit
    threadsRunning = false;
    pthread_cond_broadcast(&workCond);
    
    // Wait for threads to finish
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threadPool[i], NULL);
    }
    
    // Clean up mutex and cond var
    pthread_mutex_destroy(&workMutex);
    pthread_cond_destroy(&workCond);
    
    // Free buffers
    free(biomeCache);
    free(rgbBuffer);
    
    // Free cache
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (tileCache[i].image) {
            free(tileCache[i].image);
        }
    }
}

static int generateTile(int64_t seed, int zoom, int x, int z) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Check cache first
    size_t cachedSize;
    unsigned char* cachedImage = checkCache(seed, zoom, x, z, &cachedSize);
    if (cachedImage != NULL) {
        char outputPath[MAX_PATH_LENGTH];
        if (createOutputPath(outputPath, sizeof(outputPath), seed, zoom, x, z) < 0) {
            fprintf(stderr, "Failed to create output path\n");
            return 1;
        }
        
        int imgSize = sqrt(cachedSize/3);
        if (savePNG(outputPath, cachedImage, imgSize, imgSize) != 0) {
            fprintf(stderr, "Error saving image %s: %s\n", outputPath, strerror(errno));
            return 1;
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        printf("Cached tile %" PRId64 "/%d/%d/%d saved in %.3f seconds\n", seed, zoom, x, z, elapsed);
        return 0;
    }
    
    Generator g;
    setupGenerator(&g, MC_1_20, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    int tileSize = getTileSize(zoom);
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

    genBiomes(&g, biomeCache, r);
    parallelBiomesToImage(rgbBuffer, biomeColors, biomeCache, imgWidth, imgHeight, PIXELS_PER_CELL);

    char outputPath[MAX_PATH_LENGTH];
    if (createOutputPath(outputPath, sizeof(outputPath), seed, zoom, x, z) < 0) {
        fprintf(stderr, "Failed to create output path\n");
        return 1;
    }
    
    if (savePNG(outputPath, rgbBuffer, imgWidth, imgHeight) != 0) {
        fprintf(stderr, "Error saving image %s: %s\n", outputPath, strerror(errno));
        return 1;
    }
    
    // Add to cache
    size_t imageSize = imgWidth * imgHeight * 3;
    unsigned char* cachedCopy = malloc(imageSize);
    if (cachedCopy) {
        memcpy(cachedCopy, rgbBuffer, imageSize);
        addToCache(seed, zoom, x, z, cachedCopy, imageSize);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Tile %" PRId64 "/%d/%d/%d generated in %.3f seconds\n", seed, zoom, x, z, elapsed);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 5 || (argc-1) % 4 != 0) {
        fprintf(stderr, "Usage: %s <seed> <zoom1> <x1> <z1> [<zoom2> <x2> <z2> ...]\n", argv[0]);
        return 1;
    }
    
    // Validate seed format
    if (!isValidSeed(argv[1])) {
        fprintf(stderr, "Error: Seed must be a number (positive or negative) with up to %d digits\n", MAX_SEED_LENGTH);
        return 1;
    }
    
    // Convert seed string to int64_t
    char *endptr;
    int64_t seed = strtoll(argv[1], &endptr, 10);
    if (*endptr != '\0' || seed == LLONG_MIN || seed == LLONG_MAX) {
        fprintf(stderr, "Error: Invalid seed value (out of range)\n");
        return 1;
    }
    
    initialize();
    
    int result = 0;
    
    for (int i = 1; i < argc; i += 4) {
        int zoom = atoi(argv[i+1]);
        int x = atoi(argv[i+2]);
        int z = atoi(argv[i+3]);
        
        if (generateTile(seed, zoom, x, z) != 0) {
            result = 1;
            break;
        }
    }
    
    cleanup();
    return result;
}