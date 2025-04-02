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
#include <sys/sysinfo.h>

#define MAX_PATH_LENGTH 512
#define DEFAULT_TILE_SIZE 16
#define PIXELS_PER_CELL 4
#define CUBIOMES_SCALE 4
#define DIR_PERMISSIONS 0777
#define CACHE_SIZE 50
#define MAX_SEED_LENGTH 19
#define MAX_QUEUE_SIZE 2
#define MAX_LOAD_AVERAGE 3.0
#define WORK_DELAY_US 10000
#define MAX_THREADS 4  // Fixed maximum number of threads

typedef struct {
    int64_t seed;
    int zoom;
    int x;
    int z;
    unsigned char* image;
    size_t size;
    time_t last_accessed;
} TileCache;

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

typedef struct {
    pthread_t threads[MAX_THREADS];
    bool running;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    WorkItem work[MAX_THREADS];
    TileCache cache[CACHE_SIZE];
    int cache_index;
    int *biome_cache;
    unsigned char *rgb_buffer;
    unsigned char biome_colors[256][3];
    int num_threads;
} ThreadPool;

static ThreadPool thread_pool;

static bool isValidSeed(const char *str) {
    if (!str || !*str) return false;
    size_t len = strlen(str);
    if (len > MAX_SEED_LENGTH + 1) return false;
    
    const char *p = str;
    if (*p == '-' || *p == '+') p++;
    if (!*p) return false;
    
    while (*p) {
        if (!isdigit(*p++)) return false;
    }
    return true;
}

static int getTileSize(int zoom) {
    static const int ZOOM_TILE_SIZES[] = {256, 128, 64, 32, 16, 16};
    static const int NUM_ZOOM_LEVELS = sizeof(ZOOM_TILE_SIZES) / sizeof(ZOOM_TILE_SIZES[0]);
    
    return (zoom >= 0 && zoom < NUM_ZOOM_LEVELS) ? ZOOM_TILE_SIZES[zoom] : DEFAULT_TILE_SIZE;
}

static int createDirectory(const char *path) {
    char tmp[MAX_PATH_LENGTH];
    strncpy(tmp, path, MAX_PATH_LENGTH - 1);
    tmp[MAX_PATH_LENGTH - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, DIR_PERMISSIONS) && errno != EEXIST) {
                fprintf(stderr, "Error creating directory %s: %s\n", tmp, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }
    return mkdir(tmp, DIR_PERMISSIONS) && errno != EEXIST ? -1 : 0;
}

static void* workerThread(void* arg) {
    while (thread_pool.running) {
        pthread_mutex_lock(&thread_pool.mutex);
        
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        
        bool work_found = false;
        int work_index = -1;
        
        for (int i = 0; i < thread_pool.num_threads; i++) {
            if (!thread_pool.work[i].done && thread_pool.work[i].rgb) {
                work_found = true;
                work_index = i;
                thread_pool.work[i].done = true;
                break;
            }
        }
        
        if (!work_found) {
            if (pthread_cond_timedwait(&thread_pool.cond, &thread_pool.mutex, &ts) == ETIMEDOUT) {
                pthread_mutex_unlock(&thread_pool.mutex);
                continue;
            }
            pthread_mutex_unlock(&thread_pool.mutex);
            continue;
        }
        
        pthread_mutex_unlock(&thread_pool.mutex);
        
        usleep(WORK_DELAY_US);
        
        WorkItem* work = &thread_pool.work[work_index];
        const int width = work->width;
        const int ppc = work->pixelsPerCell;
        const int scaled_width = width / ppc;
        
        for (int y = work->startRow; y < work->endRow && thread_pool.running; y++) {
            const int scaled_y = y / ppc;
            const int row_offset = y * width;
            const int scaled_row_offset = scaled_y * scaled_width;
            
            for (int x = 0; x < width; x++) {
                const int scaled_x = x / ppc;
                const int biome_idx = scaled_row_offset + scaled_x;
                const int pixel_idx = (row_offset + x) * 3;
                
                memcpy(&work->rgb[pixel_idx], work->biomeColors[work->biomeIds[biome_idx]], 3);
            }
        }
        
        pthread_mutex_lock(&thread_pool.mutex);
        work->rgb = NULL;
        pthread_mutex_unlock(&thread_pool.mutex);
    }
    return NULL;
}

static bool checkSystemLoad() {
    double loadavg[1];
    if (getloadavg(loadavg, 1) == -1) {
        fprintf(stderr, "Warning: Could not get system load\n");
        return true;
    }
    if (loadavg[0] > MAX_LOAD_AVERAGE) {
        fprintf(stderr, "High system load (%.2f), waiting...\n", loadavg[0]);
        sleep(1);
        return false;
    }
    return true;
}

static unsigned char* checkCache(int64_t seed, int zoom, int x, int z, size_t* size) {
    time_t now = time(NULL);
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (thread_pool.cache[i].seed == seed && 
            thread_pool.cache[i].zoom == zoom && 
            thread_pool.cache[i].x == x && 
            thread_pool.cache[i].z == z) {
            thread_pool.cache[i].last_accessed = now;
            *size = thread_pool.cache[i].size;
            return thread_pool.cache[i].image;
        }
    }
    return NULL;
}

static void addToCache(int64_t seed, int zoom, int x, int z, unsigned char* image, size_t size) {
    time_t now = time(NULL);
    time_t oldest = now;
    int oldest_index = 0;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (thread_pool.cache[i].seed == seed && 
            thread_pool.cache[i].zoom == zoom && 
            thread_pool.cache[i].x == x && 
            thread_pool.cache[i].z == z) {
            free(thread_pool.cache[i].image);
            thread_pool.cache[i].image = image;
            thread_pool.cache[i].size = size;
            thread_pool.cache[i].last_accessed = now;
            return;
        }
        if (thread_pool.cache[i].last_accessed < oldest) {
            oldest = thread_pool.cache[i].last_accessed;
            oldest_index = i;
        }
    }

    thread_pool.cache[oldest_index].seed = seed;
    thread_pool.cache[oldest_index].zoom = zoom;
    thread_pool.cache[oldest_index].x = x;
    thread_pool.cache[oldest_index].z = z;
    thread_pool.cache[oldest_index].image = image;
    thread_pool.cache[oldest_index].size = size;
    thread_pool.cache[oldest_index].last_accessed = now;
}

static int createOutputPath(char *output, size_t max_len, int64_t seed, int zoom, int x, int z) {
    char dir[MAX_PATH_LENGTH];
    int len = snprintf(dir, sizeof(dir), "/var/www/production/gme-backend/storage/app/public/tiles/%" PRId64 "/%d/%d", seed, zoom, x);
    if (len < 0 || len >= sizeof(dir)) return -1;
    if (createDirectory(dir)) return -1;
    len = snprintf(output, max_len, "%s/%d.png", dir, z);
    return (len < 0 || len >= max_len) ? -1 : 0;
}

static void initialize() {
    int cores = get_nprocs();
    thread_pool.num_threads = (cores > 2) ? cores - 1 : 1;
    if (thread_pool.num_threads > MAX_THREADS) {
        thread_pool.num_threads = MAX_THREADS;
    }

    printf("Initializing with %d threads\n", thread_pool.num_threads);
    
    size_t max_tile = 256; // Largest zoom level
    size_t max_img = max_tile * PIXELS_PER_CELL;
    size_t max_biome = max_tile * max_tile * sizeof(int);
    size_t max_rgb = 3 * max_img * max_img;
    
    thread_pool.biome_cache = malloc(max_biome);
    thread_pool.rgb_buffer = malloc(max_rgb);
    if (!thread_pool.biome_cache || !thread_pool.rgb_buffer) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    
    pthread_mutex_init(&thread_pool.mutex, NULL);
    pthread_cond_init(&thread_pool.cond, NULL);
    thread_pool.running = true;
    
    for (int i = 0; i < thread_pool.num_threads; i++) {
        if (pthread_create(&thread_pool.threads[i], NULL, workerThread, NULL)) {
            fprintf(stderr, "Thread creation failed\n");
            thread_pool.num_threads = i;
            break;
        }
    }
    
    initBiomeColors(thread_pool.biome_colors);
    memset(thread_pool.cache, 0, sizeof(thread_pool.cache));
    thread_pool.cache_index = 0;
}

static void cleanup() {
    thread_pool.running = false;
    pthread_cond_broadcast(&thread_pool.cond);
    
    for (int i = 0; i < thread_pool.num_threads; i++) {
        pthread_join(thread_pool.threads[i], NULL);
    }
    
    pthread_mutex_destroy(&thread_pool.mutex);
    pthread_cond_destroy(&thread_pool.cond);
    
    free(thread_pool.biome_cache);
    free(thread_pool.rgb_buffer);
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        free(thread_pool.cache[i].image);
    }
}

static int generateTile(int64_t seed, int zoom, int x, int z) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (!checkSystemLoad()) {
        // Retry after delay
    }

    size_t cached_size;
    unsigned char* cached = checkCache(seed, zoom, x, z, &cached_size);
    if (cached) {
        char path[MAX_PATH_LENGTH];
        if (createOutputPath(path, sizeof(path), seed, zoom, x, z)) {
            fprintf(stderr, "Path creation failed\n");
            return 1;
        }
        
        int size = sqrt(cached_size/3);
        if (savePNG(path, cached, size, size)) {
            fprintf(stderr, "Failed to save PNG: %s\n", strerror(errno));
            return 1;
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        printf("Cached tile %" PRId64 "/%d/%d/%d (%.3fs)\n", seed, zoom, x, z, elapsed);
        return 0;
    }

    Generator g;
    setupGenerator(&g, MC_1_20, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    int tile_size = getTileSize(zoom);
    int width = tile_size * PIXELS_PER_CELL;
    int height = tile_size * PIXELS_PER_CELL;

    Range r = {
        .scale = CUBIOMES_SCALE,
        .x = x * tile_size,
        .z = z * tile_size,
        .sx = tile_size,
        .sz = tile_size,
        .y = 15,
        .sy = 1
    };

    genBiomes(&g, thread_pool.biome_cache, r);
    
    pthread_mutex_lock(&thread_pool.mutex);
    int rows_per_thread = height / thread_pool.num_threads;
    int remaining = height % thread_pool.num_threads;
    int current_row = 0;
    
    for (int i = 0; i < thread_pool.num_threads; i++) {
        thread_pool.work[i].rgb = thread_pool.rgb_buffer;
        thread_pool.work[i].biomeIds = thread_pool.biome_cache;
        thread_pool.work[i].biomeColors = thread_pool.biome_colors;
        thread_pool.work[i].width = width;
        thread_pool.work[i].height = height;
        thread_pool.work[i].pixelsPerCell = PIXELS_PER_CELL;
        thread_pool.work[i].startRow = current_row;
        thread_pool.work[i].endRow = current_row + rows_per_thread + (i < remaining ? 1 : 0);
        thread_pool.work[i].done = false;
        current_row = thread_pool.work[i].endRow;
    }
    
    pthread_cond_broadcast(&thread_pool.cond);
    pthread_mutex_unlock(&thread_pool.mutex);
    
    bool complete;
    do {
        usleep(10000);
        pthread_mutex_lock(&thread_pool.mutex);
        complete = true;
        for (int i = 0; i < thread_pool.num_threads; i++) {
            if (thread_pool.work[i].rgb) {
                complete = false;
                break;
            }
        }
        pthread_mutex_unlock(&thread_pool.mutex);
    } while (!complete);

    char path[MAX_PATH_LENGTH];
    if (createOutputPath(path, sizeof(path), seed, zoom, x, z)) {
        fprintf(stderr, "Path creation failed\n");
        return 1;
    }
    
    if (savePNG(path, thread_pool.rgb_buffer, width, height)) {
        fprintf(stderr, "Failed to save PNG: %s\n", strerror(errno));
        return 1;
    }
    
    size_t img_size = width * height * 3;
    unsigned char* copy = malloc(img_size);
    if (copy) {
        memcpy(copy, thread_pool.rgb_buffer, img_size);
        addToCache(seed, zoom, x, z, copy, img_size);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Generated tile %" PRId64 "/%d/%d/%d (%.3fs)\n", seed, zoom, x, z, elapsed);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 5 || (argc-1) % 4 != 0) {
        fprintf(stderr, "Usage: %s <seed> <zoom1> <x1> <z1> [<zoom2> <x2> <z2> ...]\n", argv[0]);
        return 1;
    }
    
    if (!isValidSeed(argv[1])) {
        fprintf(stderr, "Invalid seed (max %d digits)\n", MAX_SEED_LENGTH);
        return 1;
    }
    
    char *end;
    int64_t seed = strtoll(argv[1], &end, 10);
    if (*end || seed == LLONG_MIN || seed == LLONG_MAX) {
        fprintf(stderr, "Seed out of range\n");
        return 1;
    }
    
    initialize();
    int result = 0;
    
    for (int i = 1; i < argc; i += 4) {
        int zoom = atoi(argv[i+1]);
        int x = atoi(argv[i+2]);
        int z = atoi(argv[i+3]);
        
        if (generateTile(seed, zoom, x, z)) {
            result = 1;
            break;
        }
    }
    
    cleanup();
    return result;
}