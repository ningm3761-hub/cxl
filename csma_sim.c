#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>

#define MAX_LAT 2000000

typedef struct {
    int tid;
    int mode;       // 0 = Random, 1 = Basic CSMA
    int load;       // 10 ~ 100
    int seconds;
    unsigned int seed;
} worker_arg_t;

static pthread_mutex_t channel_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stat_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile uint8_t *mem_area;
static size_t mem_size = 64 * 1024 * 1024;

static uint64_t attempts = 0;
static uint64_t success = 0;
static uint64_t retry_count = 0;
static uint64_t backoff_count = 0;

static uint64_t latencies[MAX_LAT];
static int lat_count = 0;

static int stop_flag = 0;

static uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void busy_work_ns(uint64_t ns) {
    uint64_t start = now_ns();
    while (now_ns() - start < ns) {
        asm volatile("" ::: "memory");
    }
}

static void record_latency(uint64_t ns) {
    pthread_mutex_lock(&stat_lock);
    if (lat_count < MAX_LAT) {
        latencies[lat_count++] = ns;
    }
    pthread_mutex_unlock(&stat_lock);
}

static void inc_u64(uint64_t *x) {
    pthread_mutex_lock(&stat_lock);
    (*x)++;
    pthread_mutex_unlock(&stat_lock);
}

static void memory_request(unsigned int *seed) {
    size_t offset = (rand_r(seed) % (mem_size / 64)) * 64;
    mem_area[offset]++;
    busy_work_ns(50000); // simulate CXL request service time: 50 us
}

static void random_mode(worker_arg_t *arg) {
    unsigned int seed = arg->seed;

    while (!stop_flag) {
        int idle_us = (100 - arg->load) * 20;
        if (idle_us > 0) {
            usleep(rand_r(&seed) % idle_us);
        }

        uint64_t t1 = now_ns();
        inc_u64(&attempts);

        pthread_mutex_lock(&channel_lock);
        memory_request(&seed);
        pthread_mutex_unlock(&channel_lock);

        uint64_t t2 = now_ns();
        inc_u64(&success);
        record_latency(t2 - t1);
    }
}

static void csma_mode(worker_arg_t *arg) {
    unsigned int seed = arg->seed;
    int backoff_window_us = 20;

    while (!stop_flag) {
        int idle_us = (100 - arg->load) * 20;
        if (idle_us > 0) {
            usleep(rand_r(&seed) % idle_us);
        }

        uint64_t t1 = now_ns();
        inc_u64(&attempts);

        if (pthread_mutex_trylock(&channel_lock) == 0) {
            memory_request(&seed);
            pthread_mutex_unlock(&channel_lock);

            uint64_t t2 = now_ns();
            inc_u64(&success);
            record_latency(t2 - t1);

            backoff_window_us = 20;
        } else {
            inc_u64(&retry_count);
            inc_u64(&backoff_count);

            usleep(rand_r(&seed) % backoff_window_us);

            if (backoff_window_us < 2000) {
                backoff_window_us *= 2;
            }
        }
    }
}

static void *worker(void *p) {
    worker_arg_t *arg = (worker_arg_t *)p;

    if (arg->mode == 0) {
        random_mode(arg);
    } else {
        csma_mode(arg);
    }

    return NULL;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static uint64_t percentile(double p) {
    if (lat_count == 0) return 0;
    int idx = (int)(p * (lat_count - 1));
    if (idx < 0) idx = 0;
    if (idx >= lat_count) idx = lat_count - 1;
    return latencies[idx];
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Usage: %s <mode> <load> <threads> <seconds>\n", argv[0]);
        printf("mode: 0 = Random, 1 = Basic CSMA\n");
        printf("Example: %s 0 50 4 10\n", argv[0]);
        return 1;
    }

    int mode = atoi(argv[1]);
    int load = atoi(argv[2]);
    int threads = atoi(argv[3]);
    int seconds = atoi(argv[4]);

    if (mode != 0 && mode != 1) {
        printf("Error: mode must be 0 or 1\n");
        return 1;
    }

    if (load < 1 || load > 100) {
        printf("Error: load must be between 1 and 100\n");
        return 1;
    }

    mem_area = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mem_area == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    pthread_t *tids = malloc(sizeof(pthread_t) * threads);
    worker_arg_t *args = malloc(sizeof(worker_arg_t) * threads);

    uint64_t start = now_ns();

    for (int i = 0; i < threads; i++) {
        args[i].tid = i;
        args[i].mode = mode;
        args[i].load = load;
        args[i].seconds = seconds;
        args[i].seed = 1234 + i * 17;

        pthread_create(&tids[i], NULL, worker, &args[i]);
    }

    sleep(seconds);
    stop_flag = 1;

    for (int i = 0; i < threads; i++) {
        pthread_join(tids[i], NULL);
    }

    uint64_t end = now_ns();
    double elapsed_s = (end - start) / 1000000000.0;

    qsort(latencies, lat_count, sizeof(uint64_t), cmp_u64);

    uint64_t p50 = percentile(0.50);
    uint64_t p95 = percentile(0.95);
    uint64_t p99 = percentile(0.99);

    double goodput = success / elapsed_s;

    printf("mode,load,threads,seconds,attempts,success,retry,backoff,goodput,delay_p50_ns,delay_p95_ns,delay_p99_ns\n");
    printf("%s,%d,%d,%d,%lu,%lu,%lu,%lu,%.2f,%lu,%lu,%lu\n",
           mode == 0 ? "random" : "csma",
           load,
           threads,
           seconds,
           attempts,
           success,
           retry_count,
           backoff_count,
           goodput,
           p50,
           p95,
           p99);

    munmap((void *)mem_area, mem_size);
    free(tids);
    free(args);

    return 0;
}
