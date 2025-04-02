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

static int NUM_THREADS = 2;
static const int ZOOM_TILE_SIZES[] = {256, 128, 64, 32, 16, 16};
#define NUM_ZOOM_LEVELS (sizeof(ZOOM_TILE_SIZES) / sizeof(ZOOM_TILE_SIZES[0]))

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

static struct {
    pthread_t threads[NUM_THREADS];
    bool running;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    WorkItem work[NUM_THREADS];
    TileCache cache[CACHE_SIZE];
    int cache_index;
    int *biome_cache;
    unsigned char *rgb_buffer;
    unsigned char biome_colors[256][3];
} ThreadPool;

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
    return (zoom >= 0 && zoom < (int)NUM_ZOOM_LEVELS) ? ZOOM_TILE_SIZES[zoom] : DEFAULT_TILE_SIZE;
}

static int createDirectory(const char *path) {
    char tmp[MAX_PATH_LENGTH];
    strncpy(tmp, path, MAX_PATH_LENGTH - 1);
    tmp[MAX_PATH_LENGTH - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, DIR_PERMISSIONS) {
                if (errno != EEXIST) {
                    fprintf(stderr, "Error creating directory %s: %s\n", tmp, strerror(errno));
                    return -1;
                }
            }
            *p = '/';
        }
    }
    return mkdir(tmp, DIR_PERMISSIONS) && errno != EEXIST ? -1 : 0;
}

static void* workerThread(void* arg) {
    while (ThreadPool.running) {
        pthread_mutex_lock(&ThreadPool.mutex);
        
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        
        bool work_found = false;
        int work_index = -1;
        
        for (int i = 0; i < NUM_THREADS; i++) {
            if (!ThreadPool.work[i].done && ThreadPool.work[i].rgb) {
                work_found = true;
                work_index = i;
                ThreadPool.work[i].done = true;
                break;
            }
        }
        
        if (!work_found) {
            if (pthread_cond_timedwait(&ThreadPool.cond, &ThreadPool.mutex, &ts) == ETIMEDOUT) {
                pthread_mutex_unlock(&ThreadPool.mutex);
                continue;
            }
            pthread_mutex_unlock(&ThreadPool.mutex);
            continue;
        }
        
        pthread_mutex_unlock(&ThreadPool.mutex);
        
        usleep(WORK_DELAY_US);
        
        WorkItem* work = &ThreadPool.work[work_index];
        const int width = work->width;
        const int ppc = work->pixelsPerCell;
        const int scaled_width = width / ppc;
        
        for (int y = work->startRow; y < work->endRow && ThreadPool.running; y++) {
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
        
        pthread_mutex_lock(&ThreadPool.mutex);
        work->rgb = NULL;
        pthread_mutex_unlock(&ThreadPool.mutex);
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
        if (ThreadPool.cache[i].seed == seed && 
            ThreadPool.cache[i].zoom == zoom && 
            ThreadPool.cache[i].x == x && 
            ThreadPool.cache[i].z == z) {
            ThreadPool.cache[i].last_accessed = now;
            *size = ThreadPool.cache[i].size;
            return ThreadPool.cache[i].image;
        }
    }
    return NULL;
}

static void addToCache(int64_t seed, int zoom, int x, int z, unsigned char* image, size_t size) {
    time_t now = time(NULL);
    time_t oldest = now;
    int oldest_index = 0;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (ThreadPool.cache[i].seed == seed && 
            ThreadPool.cache[i].zoom == zoom && 
            ThreadPool.cache[i].x == x && 
            ThreadPool.cache[i].z == z) {
            free(ThreadPool.cache[i].image);
            ThreadPool.cache[i].image = image;
            ThreadPool.cache[i].size = size;
            ThreadPool.cache[i].last_accessed = now;
            return;
        }
        if (ThreadPool.cache[i].last_accessed < oldest) {
            oldest = ThreadPool.cache[i].last_accessed;
            oldest_index = i;
        }
    }

    ThreadPool.cache[oldest_index].seed = seed;
    ThreadPool.cache[oldest_index].zoom = zoom;
    ThreadPool.cache[oldest_index].x = x;
    ThreadPool.cache[oldest_index].z = z;
    ThreadPool.cache[oldest_index].image = image;
    ThreadPool.cache[oldest_index].size = size;
    ThreadPool.cache[oldest_index].last_accessed = now;
}

static int createOutputPath(char *output, size_t max_len, int64_t seed, int zoom, int x, int z) {
    char dir[MAX_PATH_LENGTH];
    int len = snprintf(dir, sizeof(dir), "/var/www/storage/app/public/tiles/%" PRId64 "/%d/%d", seed, zoom, x);
    if (len < 0 || len >= sizeof(dir)) return -1;
    if (createDirectory(dir)) return -1;
    len = snprintf(output, max_len, "%s/%d.png", dir, z);
    return (len < 0 || len >= max_len) ? -1 : 0;
}

static void initialize() {
    NUM_THREADS = get_nprocs();
    if (NUM_THREADS > 3) NUM_THREADS--;
    if (NUM_THREADS < 1) NUM_THREADS = 1;

    printf("Initializing with %d threads\n", NUM_THREADS);
    
    size_t max_tile = ZOOM_TILE_SIZES[0];
    size_t max_img = max_tile * PIXELS_PER_CELL;
    size_t max_biome = max_tile * max_tile * sizeof(int);
    size_t max_rgb = 3 * max_img * max_img;
    
    ThreadPool.biome_cache = malloc(max_biome);
    ThreadPool.rgb_buffer = malloc(max_rgb);
    if (!ThreadPool.biome_cache || !ThreadPool.rgb_buffer) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    
    pthread_mutex_init(&ThreadPool.mutex, NULL);
    pthread_cond_init(&ThreadPool.cond, NULL);
    ThreadPool.running = true;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&ThreadPool.threads[i], NULL, workerThread, NULL)) {
            fprintf(stderr, "Thread creation failed\n");
            NUM_THREADS = i;
            break;
        }
    }
    
    initBiomeColors(ThreadPool.biome_colors);
    memset(ThreadPool.cache, 0, sizeof(ThreadPool.cache));
    ThreadPool.cache_index = 0;
}

static void cleanup() {
    ThreadPool.running = false;
    pthread_cond_broadcast(&ThreadPool.cond);
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(ThreadPool.threads[i], NULL);
    }
    
    pthread_mutex_destroy(&ThreadPool.mutex);
    pthread_cond_destroy(&ThreadPool.cond);
    
    free(ThreadPool.biome_cache);
    free(ThreadPool.rgb_buffer);
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        free(ThreadPool.cache[i].image);
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

    genBiomes(&g, ThreadPool.biome_cache, r);
    
    pthread_mutex_lock(&ThreadPool.mutex);
    int rows_per_thread = height / NUM_THREADS;
    int remaining = height % NUM_THREADS;
    int current_row = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        ThreadPool.work[i].rgb = ThreadPool.rgb_buffer;
        ThreadPool.work[i].biomeIds = ThreadPool.biome_cache;
        ThreadPool.work[i].biomeColors = ThreadPool.biome_colors;
        ThreadPool.work[i].width = width;
        ThreadPool.work[i].height = height;
        ThreadPool.work[i].pixelsPerCell = PIXELS_PER_CELL;
        ThreadPool.work[i].startRow = current_row;
        ThreadPool.work[i].endRow = current_row + rows_per_thread + (i < remaining ? 1 : 0);
        ThreadPool.work[i].done = false;
        current_row = ThreadPool.work[i].endRow;
    }
    
    pthread_cond_broadcast(&ThreadPool.cond);
    pthread_mutex_unlock(&ThreadPool.mutex);
    
    bool complete;
    do {
        usleep(10000);
        pthread_mutex_lock(&ThreadPool.mutex);
        complete = true;
        for (int i = 0; i < NUM_THREADS; i++) {
            if (ThreadPool.work[i].rgb) {
                complete = false;
                break;
            }
        }
        pthread_mutex_unlock(&ThreadPool.mutex);
    } while (!complete);

    char path[MAX_PATH_LENGTH];
    if (createOutputPath(path, sizeof(path), seed, zoom, x, z)) {
        fprintf(stderr, "Path creation failed\n");
        return 1;
    }
    
    if (savePNG(path, ThreadPool.rgb_buffer, width, height)) {
        fprintf(stderr, "Failed to save PNG: %s\n", strerror(errno));
        return 1;
    }
    
    size_t img_size = width * height * 3;
    unsigned char* copy = malloc(img_size);
    if (copy) {
        memcpy(copy, ThreadPool.rgb_buffer, img_size);
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