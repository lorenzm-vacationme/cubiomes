// #include "generator.h"
// #include "util.h"
// #include "image_utils.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <sys/stat.h>
// #include <sys/types.h>
// #include <errno.h>
// #include <string.h>
// #include <pthread.h>
// #include <stdint.h>
// #include <inttypes.h>

// #define MAX_PATH_LENGTH 512
// #define DEFAULT_TILE_SIZE 16
// #define PIXELS_PER_CELL 4
// #define CUBIOMES_SCALE 4
// #define DIR_PERMISSIONS 0777
// #define NUM_THREADS 3

// // Lookup table for zoom levels to tile sizes
// static const int ZOOM_TILE_SIZES[] = {256, 128, 64, 32, 16, 16};
// #define NUM_ZOOM_LEVELS (sizeof(ZOOM_TILE_SIZES) / sizeof(ZOOM_TILE_SIZES[0]))

// static int getTileSize(int zoom) {
//     if (zoom >= 0 && zoom < NUM_ZOOM_LEVELS) {
//         return ZOOM_TILE_SIZES[zoom];
//     }
//     return DEFAULT_TILE_SIZE;
// }

// static int createSingleDirectory(const char *path) {
//     if (mkdir(path, DIR_PERMISSIONS) != 0 && errno != EEXIST) {
//         fprintf(stderr, "Error creating directory %s: %s\n", path, strerror(errno));
//         return -1;
//     }
//     return 0;
// }

// static int createDirectories(const char *path) {
//     char tmp[MAX_PATH_LENGTH];
//     char *p;
    
//     if (strlen(path) >= MAX_PATH_LENGTH) {
//         fprintf(stderr, "Path too long\n");
//         return -1;
//     }
    
//     strncpy(tmp, path, MAX_PATH_LENGTH - 1);
//     tmp[MAX_PATH_LENGTH - 1] = '\0';
    
//     for (p = tmp + 1; *p; p++) {
//         if (*p == '/') {
//             *p = '\0';
//             if (createSingleDirectory(tmp) < 0) return -1;
//             *p = '/';
//         }
//     }
    
//     return createSingleDirectory(tmp);
// }

// typedef struct {
//     unsigned char *rgb;
//     const int *biomeIds;
//     const unsigned char (*biomeColors)[3];
//     int startRow;
//     int endRow;
//     int width;
//     int height;
//     int pixelsPerCell;
// } ThreadData;

// static void *processChunk(void *arg) {
//     ThreadData *data = (ThreadData *)arg;
//     const int width = data->width;
//     const int pixelsPerCell = data->pixelsPerCell;
//     const int scaledWidth = width / pixelsPerCell;
    
//     for (int y = data->startRow; y < data->endRow; y++) {
//         const int scaledY = y / pixelsPerCell;
//         const int rowOffset = y * width;
//         const int scaledRowOffset = scaledY * scaledWidth;
        
//         for (int x = 0; x < width; x++) {
//             const int scaledX = x / pixelsPerCell;
//             const int biomeIdx = scaledRowOffset + scaledX;
//             const int pixelIdx = (rowOffset + x) * 3;
            
//             memcpy(&data->rgb[pixelIdx], 
//                    data->biomeColors[data->biomeIds[biomeIdx]], 
//                    3);
//         }
//     }
    
//     return NULL;
// }

// static void parallelBiomesToImage(unsigned char *rgb, 
//                                 const unsigned char biomeColors[][3], 
//                                 const int *biomeIds, 
//                                 int width, int height, 
//                                 int pixelsPerCell) {
//     pthread_t threads[NUM_THREADS];
//     ThreadData threadData[NUM_THREADS];
    
//     int rowsPerThread = height / NUM_THREADS;
//     int remainingRows = height % NUM_THREADS;
    
//     int currentRow = 0;
//     for (int i = 0; i < NUM_THREADS; i++) {
//         threadData[i].rgb = rgb;
//         threadData[i].biomeIds = biomeIds;
//         threadData[i].biomeColors = biomeColors;
//         threadData[i].width = width;
//         threadData[i].height = height;
//         threadData[i].pixelsPerCell = pixelsPerCell;
//         threadData[i].startRow = currentRow;
        
//         threadData[i].endRow = currentRow + rowsPerThread;
//         if (i < remainingRows) {
//             threadData[i].endRow++;
//         }
//         currentRow = threadData[i].endRow;
        
//         pthread_create(&threads[i], NULL, processChunk, &threadData[i]);
//     }
    
//     for (int i = 0; i < NUM_THREADS; i++) {
//         pthread_join(threads[i], NULL);
//     }
// }

// // static int createOutputPath(char *outputPath, size_t maxLen, int64_t seed, int zoom, int x, int z) {
// //     char basePath[] = "/var/www/production/gme-backend/storage/app/public/tiles";
// //     char tmpPath[MAX_PATH_LENGTH];

// //     int dirLen = snprintf(tmpPath, sizeof(tmpPath), "%s/%ld/%d/%d", basePath, seed, zoom, x);
// //     if (dirLen < 0 || dirLen >= sizeof(tmpPath)) return -1;

// //     if (createDirectories(tmpPath) < 0) return -1;

// //     int fullLen = snprintf(outputPath, maxLen, "%s/%d.png", tmpPath, z);
// //     if (fullLen < 0 || fullLen >= maxLen) return -1;

// //     return 0;
// // }

// // int createOutputPath(char *outputPath, size_t maxLen, int64_t seed, int zoom, int x, int z) {
//     int createOutputPath(char *outputPath, size_t maxLen, uint64_t seed, int zoom, int x, int z) {
//     char basePath[] = "/var/www/production/gme-backend/storage/app/public/tiles";
//     // char basePath[] = "/var/www/storage/app/public/tiles/";
//     char tmpPath[MAX_PATH_LENGTH];

//     // Use PRId64 to ensure proper formatting for int64_t
//     // int dirLen = snprintf(tmpPath, sizeof(tmpPath), "%s/%" PRId64 "/%d/%d", basePath, seed, zoom, x);
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

//     int fullLen = snprintf(outputPath, maxLen, "%s/%d.png", tmpPath, z);
//     if (fullLen < 0 || fullLen >= maxLen) {
//         fprintf(stderr, "Output path snprintf failed\n");
//         return -1;
//     }

//     printf("Final output path: %s\n", outputPath);  // Debugging output
//     return 0;
// }

// // uint64_t parseSeed(const char *str) {
// //     char *endptr;
// //     errno = 0;
    
// //     // First try to parse as a regular number
// //     unsigned long long ull_result = strtoull(str, &endptr, 10);
    
// //     // Check for overflow
// //     if (errno == ERANGE || *endptr != '\0') {
// //         fprintf(stderr, "Seed value out of range or invalid format: %s\n", str);
// //         fprintf(stderr, "Using hash of seed string instead\n");
        
// //         // Use a simple hash function to convert the string to a uint64_t
// //         uint64_t hash = 5381;
// //         int c;
// //         const char *ptr = str;
        
// //         while ((c = *ptr++)) {
// //             hash = ((hash << 5) + hash) + c; // hash * 33 + c
// //         }
        
// //         fprintf(stderr, "Generated hash seed: %" PRIu64 "\n", hash);
// //         return hash;
// //     }
    
// //     // Use the appropriate format specifier for unsigned long long
// //     fprintf(stderr, "Successfully parsed seed: %llu\n", ull_result);
    
// //     // Return the result cast to uint64_t
// //     return (uint64_t)ull_result;
// // }

// uint64_t parseSeed(const char *str) {
//     // Fast path: try a quick hash for very large values
//     size_t len = strlen(str);
//     if (len > 18) {  // A uint64_t can hold at most 20 digits
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


// int main(int argc, char *argv[]) {
//     if (argc < 5) {
//         fprintf(stderr, "Usage: %s <seed> <zoom> <x> <z>\n", argv[0]);
//         return 1;
//     }

//     // int64_t seed = parseSeed(argv[1]);
//     uint64_t seed = parseSeed(argv[1]);
//     int zoom = atoi(argv[2]);
//     int x = atoi(argv[3]);
//     int z = atoi(argv[4]);

//     // printf("Seed: %" PRId64 ", Zoom: %d, X: %d, Z: %d\n", seed, zoom, x, z);
//     printf("Seed: %" PRIu64 ", Zoom: %d, X: %d, Z: %d\n", seed, zoom, x, z);

//     Generator g;
//     setupGenerator(&g, MC_1_20, 0);
//     applySeed(&g, DIM_OVERWORLD, seed);

//     int tileSize = getTileSize(zoom);
//     printf("Tile Size: %d\n", tileSize);

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

//     genBiomes(&g, biomeIds, r);

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

//     if (savePNG(outputPath, rgb, imgWidth, imgHeight) != 0) {
//         fprintf(stderr, "Error saving image %s\n", outputPath);
//         free(biomeIds);
//         free(rgb);
//         return 1;
//     }

//     printf("Saved: %s\n", outputPath);
//     free(biomeIds);
//     free(rgb);
//     return 0;
// }

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
#include <unistd.h>

#define MAX_PATH_LENGTH 512
#define DEFAULT_TILE_SIZE 16
#define PIXELS_PER_CELL 4
#define CUBIOMES_SCALE 4
#define DIR_PERMISSIONS 0777

// Increase thread count for better CPU utilization
#define NUM_THREADS 8

// Add buffer pool size to reuse memory
#define BUFFER_POOL_SIZE 4

// Cache directory existence to avoid repeated syscalls
#define DIR_CACHE_SIZE 128

// Lookup table for zoom levels to tile sizes
static const int ZOOM_TILE_SIZES[] = {256, 128, 64, 32, 16, 16};
#define NUM_ZOOM_LEVELS (sizeof(ZOOM_TILE_SIZES) / sizeof(ZOOM_TILE_SIZES[0]))

// Directory existence cache
typedef struct {
    char path[MAX_PATH_LENGTH];
    int exists;
} DirCacheEntry;

static DirCacheEntry dirCache[DIR_CACHE_SIZE];
static int dirCacheCount = 0;
static pthread_mutex_t dirCacheMutex = PTHREAD_MUTEX_INITIALIZER;

// Memory pool for reusing buffers
typedef struct {
    int *biomeIds[BUFFER_POOL_SIZE];
    unsigned char *rgbBuffers[BUFFER_POOL_SIZE];
    int inUse[BUFFER_POOL_SIZE];
    int sizes[BUFFER_POOL_SIZE];
    pthread_mutex_t mutex;
} BufferPool;

static BufferPool bufferPool = {0};

typedef struct {
    char* originalSeedStr;  // Store the original seed string
    uint64_t hashedSeed;    // Store the hashed seed value for generation
} SeedInfo;

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

// Initialize the buffer pool
void initBufferPool() {
    pthread_mutex_init(&bufferPool.mutex, NULL);
    memset(bufferPool.inUse, 0, sizeof(bufferPool.inUse));
    memset(bufferPool.sizes, 0, sizeof(bufferPool.sizes));
}

// Get a buffer from the pool or allocate new one
int* getBiomeBuffer(int size) {
    pthread_mutex_lock(&bufferPool.mutex);
    
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (!bufferPool.inUse[i] && bufferPool.biomeIds[i] && bufferPool.sizes[i] >= size) {
            bufferPool.inUse[i] = 1;
            pthread_mutex_unlock(&bufferPool.mutex);
            return bufferPool.biomeIds[i];
        }
    }
    
    // Allocate new buffer if none available
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (!bufferPool.biomeIds[i]) {
            bufferPool.biomeIds[i] = malloc(sizeof(int) * size);
            bufferPool.sizes[i] = size;
            bufferPool.inUse[i] = 1;
            pthread_mutex_unlock(&bufferPool.mutex);
            return bufferPool.biomeIds[i];
        }
    }
    
    // If all slots are taken, allocate without caching
    pthread_mutex_unlock(&bufferPool.mutex);
    return malloc(sizeof(int) * size);
}

// Get an RGB buffer from the pool or allocate new one
unsigned char* getRgbBuffer(int size) {
    pthread_mutex_lock(&bufferPool.mutex);
    
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (!bufferPool.inUse[i] && bufferPool.rgbBuffers[i] && bufferPool.sizes[i] >= size) {
            bufferPool.inUse[i] = 1;
            pthread_mutex_unlock(&bufferPool.mutex);
            return bufferPool.rgbBuffers[i];
        }
    }
    
    // Allocate new buffer if none available
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (!bufferPool.rgbBuffers[i]) {
            bufferPool.rgbBuffers[i] = malloc(size);
            bufferPool.sizes[i] = size;
            bufferPool.inUse[i] = 1;
            pthread_mutex_unlock(&bufferPool.mutex);
            return bufferPool.rgbBuffers[i];
        }
    }
    
    // If all slots are taken, allocate without caching
    pthread_mutex_unlock(&bufferPool.mutex);
    return malloc(size);
}

// Return a buffer to the pool
void returnBuffer(void* buffer) {
    pthread_mutex_lock(&bufferPool.mutex);
    
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (bufferPool.biomeIds[i] == buffer || bufferPool.rgbBuffers[i] == buffer) {
            bufferPool.inUse[i] = 0;
            pthread_mutex_unlock(&bufferPool.mutex);
            return;
        }
    }
    
    // If not in pool, free it
    free(buffer);
    pthread_mutex_unlock(&bufferPool.mutex);
}

// Clean up buffer pool
void cleanupBufferPool() {
    pthread_mutex_lock(&bufferPool.mutex);
    
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (bufferPool.biomeIds[i]) {
            free(bufferPool.biomeIds[i]);
            bufferPool.biomeIds[i] = NULL;
        }
        if (bufferPool.rgbBuffers[i]) {
            free(bufferPool.rgbBuffers[i]);
            bufferPool.rgbBuffers[i] = NULL;
        }
    }
    
    pthread_mutex_unlock(&bufferPool.mutex);
    pthread_mutex_destroy(&bufferPool.mutex);
}

static int getTileSize(int zoom) {
    if (zoom >= 0 && zoom < NUM_ZOOM_LEVELS) {
        return ZOOM_TILE_SIZES[zoom];
    }
    return DEFAULT_TILE_SIZE;
}

static int directoryExists(const char *path) {
    // Check cache first
    pthread_mutex_lock(&dirCacheMutex);
    for (int i = 0; i < dirCacheCount; i++) {
        if (strcmp(dirCache[i].path, path) == 0) {
            int result = dirCache[i].exists;
            pthread_mutex_unlock(&dirCacheMutex);
            return result;
        }
    }
    pthread_mutex_unlock(&dirCacheMutex);
    
    // Not in cache, check filesystem
    struct stat st;
    int result = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    
    // Add to cache
    pthread_mutex_lock(&dirCacheMutex);
    if (dirCacheCount < DIR_CACHE_SIZE) {
        strncpy(dirCache[dirCacheCount].path, path, MAX_PATH_LENGTH - 1);
        dirCache[dirCacheCount].path[MAX_PATH_LENGTH - 1] = '\0';
        dirCache[dirCacheCount].exists = result;
        dirCacheCount++;
    }
    pthread_mutex_unlock(&dirCacheMutex);
    
    return result;
}

static int createSingleDirectory(const char *path) {
    // Check if directory already exists to avoid unnecessary syscall
    if (directoryExists(path)) {
        return 0;
    }
    
    if (mkdir(path, DIR_PERMISSIONS) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error creating directory %s: %s\n", path, strerror(errno));
        return -1;
    }
    
    // Update cache
    pthread_mutex_lock(&dirCacheMutex);
    if (dirCacheCount < DIR_CACHE_SIZE) {
        strncpy(dirCache[dirCacheCount].path, path, MAX_PATH_LENGTH - 1);
        dirCache[dirCacheCount].path[MAX_PATH_LENGTH - 1] = '\0';
        dirCache[dirCacheCount].exists = 1;
        dirCacheCount++;
    }
    pthread_mutex_unlock(&dirCacheMutex);
    
    return 0;
}

static int createDirectories(const char *path) {
    // Fast path: check if directory already exists
    if (directoryExists(path)) {
        return 0;
    }
    
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

static void *processChunk(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    const int width = data->width;
    const int pixelsPerCell = data->pixelsPerCell;
    const int scaledWidth = width / pixelsPerCell;
    
    // Use local variables to avoid pointer dereferencing in the loop
    unsigned char *rgb = data->rgb;
    const int *biomeIds = data->biomeIds;
    const unsigned char (*biomeColors)[3] = data->biomeColors;
    
    // Calculate chunk size for improved cache locality
    const int chunkSize = 16;
    
    // Process by chunk for better cache locality
    for (int yc = data->startRow; yc < data->endRow; yc += chunkSize) {
        int yEnd = (yc + chunkSize < data->endRow) ? yc + chunkSize : data->endRow;
        
        for (int xc = 0; xc < width; xc += chunkSize) {
            int xEnd = (xc + chunkSize < width) ? xc + chunkSize : width;
            
            for (int y = yc; y < yEnd; y++) {
                const int scaledY = y / pixelsPerCell;
                const int rowOffset = y * width;
                const int scaledRowOffset = scaledY * scaledWidth;
                
                for (int x = xc; x < xEnd; x++) {
                    const int scaledX = x / pixelsPerCell;
                    const int biomeIdx = scaledRowOffset + scaledX;
                    const int pixelIdx = (rowOffset + x) * 3;
                    
                    // Inline the memcpy for better performance
                    rgb[pixelIdx] = biomeColors[biomeIds[biomeIdx]][0];
                    rgb[pixelIdx+1] = biomeColors[biomeIds[biomeIdx]][1];
                    rgb[pixelIdx+2] = biomeColors[biomeIds[biomeIdx]][2];
                }
            }
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
    
    // Adjust this to split work more evenly
    int rowsPerThread = (height + NUM_THREADS - 1) / NUM_THREADS;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        threadData[i].rgb = rgb;
        threadData[i].biomeIds = biomeIds;
        threadData[i].biomeColors = biomeColors;
        threadData[i].width = width;
        threadData[i].height = height;
        threadData[i].pixelsPerCell = pixelsPerCell;
        threadData[i].startRow = i * rowsPerThread;
        threadData[i].endRow = (i + 1) * rowsPerThread;
        
        if (threadData[i].endRow > height) {
            threadData[i].endRow = height;
        }
        
        // Only create threads if there's work to do
        if (threadData[i].startRow < threadData[i].endRow) {
            pthread_create(&threads[i], NULL, processChunk, &threadData[i]);
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        if (threadData[i].startRow < threadData[i].endRow) {
            pthread_join(threads[i], NULL);
        }
    }
}

uint64_t computeSeedHash(const char *str) {
    uint64_t hash = 5381;
    int c;
    const char *ptr = str;
    
    while ((c = *ptr++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

SeedInfo parseSeed(const char *str) {
    SeedInfo info = {0};
    
    // Store the original string (make a copy)
    info.originalSeedStr = strdup(str);
    if (!info.originalSeedStr) {
        fprintf(stderr, "Failed to allocate memory for seed string\n");
        exit(1);
    }
    
    // Compute the hash for generation purposes
    info.hashedSeed = computeSeedHash(str);
    
    fprintf(stderr, "Original seed: %s, Hashed seed: %" PRIu64 "\n", 
            info.originalSeedStr, info.hashedSeed);
    
    return info;
}

int createOutputPath(char *outputPath, size_t maxLen, const char* originalSeedStr, int zoom, int x, int z) {
    static char basePath[] = "/var/www/production/gme-backend/storage/app/public/tiles";
    char tmpPath[MAX_PATH_LENGTH];

    // Create path components separately for better caching
    // First create base/seed directory
    snprintf(tmpPath, sizeof(tmpPath), "%s/%s", basePath, originalSeedStr);
    if (createDirectories(tmpPath) < 0) {
        return -1;
    }
    
    // Then create base/seed/zoom directory
    snprintf(tmpPath, sizeof(tmpPath), "%s/%s/%d", basePath, originalSeedStr, zoom);
    if (createDirectories(tmpPath) < 0) {
        return -1;
    }
    
    // Finally create base/seed/zoom/x directory
    snprintf(tmpPath, sizeof(tmpPath), "%s/%s/%d/%d", basePath, originalSeedStr, zoom, x);
    if (createDirectories(tmpPath) < 0) {
        return -1;
    }

    int fullLen = snprintf(outputPath, maxLen, "%s/%d.png", tmpPath, z);
    if (fullLen < 0 || fullLen >= maxLen) {
        fprintf(stderr, "Output path snprintf failed\n");
        return -1;
    }

    return 0;
}

int generateTile(Generator *g, SeedInfo *seedInfo, int zoom, int x, int z) {
    // Generate a single tile and save it
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

    // Get buffers from pool instead of allocating new memory each time
    int *biomeIds = getBiomeBuffer(r.sx * r.sz);
    if (!biomeIds) {
        fprintf(stderr, "Failed to allocate biome cache\n");
        return 1;
    }

    unsigned char *rgb = getRgbBuffer(3 * imgWidth * imgHeight);
    if (!rgb) {
        fprintf(stderr, "Failed to allocate RGB buffer\n");
        returnBuffer(biomeIds);
        return 1;
    }

    // Generate the biomes
    genBiomes(g, biomeIds, r);

    // Static color table to avoid reinitialization
    static unsigned char biomeColors[256][3];
    static int colorsInitialized = 0;
    
    if (!colorsInitialized) {
        initBiomeColors(biomeColors);
        colorsInitialized = 1;
    }
    
    // Convert biomes to image
    parallelBiomesToImage(rgb, biomeColors, biomeIds, imgWidth, imgHeight, PIXELS_PER_CELL);

    // Create output path
    char outputPath[MAX_PATH_LENGTH];
    if (createOutputPath(outputPath, sizeof(outputPath), seedInfo->originalSeedStr, zoom, x, z) < 0) {
        fprintf(stderr, "Failed to create output path\n");
        returnBuffer(biomeIds);
        returnBuffer(rgb);
        return 1;
    }

    // Save image
    if (savePNG(outputPath, rgb, imgWidth, imgHeight) != 0) {
        fprintf(stderr, "Error saving image %s\n", outputPath);
        returnBuffer(biomeIds);
        returnBuffer(rgb);
        return 1;
    }

    // Return buffers to pool instead of freeing
    returnBuffer(biomeIds);
    returnBuffer(rgb);
    
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <seed> <zoom> <x> <z>\n", argv[0]);
        return 1;
    }

    // Initialize the buffer pool
    initBufferPool();
    
    SeedInfo seedInfo = parseSeed(argv[1]);
    int zoom = atoi(argv[2]);
    int x = atoi(argv[3]);
    int z = atoi(argv[4]);

    printf("Original seed: %s, Zoom: %d, X: %d, Z: %d\n", 
           seedInfo.originalSeedStr, zoom, x, z);

    Generator g;
    setupGenerator(&g, MC_1_20, 0);
    applySeed(&g, DIM_OVERWORLD, seedInfo.hashedSeed);
    
    // Generate the tile
    int result = generateTile(&g, &seedInfo, zoom, x, z);
    
    // Clean up resources
    free(seedInfo.originalSeedStr);
    cleanupBufferPool();
    pthread_mutex_destroy(&dirCacheMutex);
    
    return result;
}