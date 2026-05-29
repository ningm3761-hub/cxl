#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#define MAX_LAT 2000000

typedef struct {
    int tid;
    int mode;
    int load;       // 1 ~ 100
    int seconds;
    unsigned int seed;
} worker_arg_t;

static pthread_mutex_t channel_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stat_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile uint8_t *mem_area;
static size_t mem_size = 64 * 1024 * 1024;

static uint64_t attempts = 0;
static uint64_t success_count = 0;
static uint64_t retry_count = 0;
static uint64_t backoff_count = 0;

static uint64_t latencies[MAX_LAT];
static int lat_count = 0;

static volatile int stop_flag = 0;

static uint64_t g_start_ns = 0;
static FILE *g_log_file = NULL;

static uint64_t now_ns(void) {
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

static void inc_stat(uint64_t *x) {
    pthread_mutex_lock(&stat_lock);
    (*x)++;
    pthread_mutex_unlock(&stat_lock);
}

static void record_latency(uint64_t ns) {
    pthread_mutex_lock(&stat_lock);
    if (lat_count < MAX_LAT) {
        latencies[lat_count++] = ns;
    }
    pthread_mutex_unlock(&stat_lock);
}

static void log_attempt(int tid, uint64_t timestamp_offset, int success, uint64_t latency) {
    if (g_log_file == NULL) return;
    pthread_mutex_lock(&stat_lock);
    fprintf(g_log_file, "%d,%lu,%d,%lu\n", tid, timestamp_offset, success, latency);
    pthread_mutex_unlock(&stat_lock);
}

static void memory_request(unsigned int *seed) {
    size_t offset = (rand_r(seed) % (mem_size / 64)) * 64;
    mem_area[offset]++;
    busy_work_ns(50000);
}

static void load_sleep(int load, unsigned int *seed) {
    if (load < 100) {
        int idle_us = (100 - load) * 20;
        if (idle_us > 0) {
            usleep(rand_r(seed) % idle_us);
        }
    }
}

/* ==================== 模式 0：无载波侦听基准线 ==================== */
static void random_mode(worker_arg_t *arg) {
    unsigned int seed = arg->seed;
    int tid = arg->tid;

    while (!stop_flag) {
        load_sleep(arg->load, &seed);

        uint64_t t1 = now_ns();
        inc_stat(&attempts);

        if (pthread_mutex_trylock(&channel_lock) == 0) {
            memory_request(&seed);
            pthread_mutex_unlock(&channel_lock);
            uint64_t t2 = now_ns();
            uint64_t lat = t2 - t1;

            inc_stat(&success_count);
            record_latency(lat);
            log_attempt(tid, t1 - g_start_ns, 1, lat);
        } else {
            inc_stat(&retry_count);
            log_attempt(tid, t1 - g_start_ns, 0, 0);
        }
    }
}

/* ==================== 模式 1：CSMA 先听后发 + 退避（修正端到端延迟） ==================== */
static void csma_mode(worker_arg_t *arg) {
    unsigned int seed = arg->seed;
    int tid = arg->tid;
    int backoff_window_us = 20;

    load_sleep(arg->load, &seed);

    while (!stop_flag) {
        // 数据包生成，记录首次尝试时间
        uint64_t pkt_start = now_ns();
        int sent = 0;

        while (!sent && !stop_flag) {
            uint64_t t1 = now_ns();   // 当前尝试时间（用于日志）
            inc_stat(&attempts);

            if (pthread_mutex_trylock(&channel_lock) == 0) {
                memory_request(&seed);
                pthread_mutex_unlock(&channel_lock);
                uint64_t t2 = now_ns();
                // 端到端延迟 = 成功时刻 - 数据包生成时刻
                uint64_t lat = t2 - pkt_start;

                inc_stat(&success_count);
                record_latency(lat);
                log_attempt(tid, pkt_start - g_start_ns, 1, lat);

                backoff_window_us = 20;  // 成功重置退避窗口
                sent = 1;
            } else {
                inc_stat(&retry_count);
                inc_stat(&backoff_count);
                // 记录失败尝试（时间戳用当前尝试开始时间）
                log_attempt(tid, t1 - g_start_ns, 0, 0);

                usleep(rand_r(&seed) % backoff_window_us);
                if (backoff_window_us < 2000) {
                    backoff_window_us *= 2;
                }
            }
        }

        if (!stop_flag) {
            load_sleep(arg->load, &seed);
        }
    }
}

/* ==================== 模式 2：AIMD CSMA（同样修正端到端延迟） ==================== */
static void aimd_mode(worker_arg_t *arg) {
    unsigned int seed = arg->seed;
    int tid = arg->tid;
    int cwnd = 1;
    int cwnd_max = 32;
    int backoff_window_us = 20;

    load_sleep(arg->load, &seed);

    while (!stop_flag) {
        int sent_in_round = 0;
        int round_failed = 0;

        for (int i = 0; i < cwnd && !stop_flag; i++) {
            // 每个数据包独立的生成时间
            uint64_t pkt_start = now_ns();
            int pkt_sent = 0;

            while (!pkt_sent && !stop_flag) {
                uint64_t t1 = now_ns();
                inc_stat(&attempts);

                if (pthread_mutex_trylock(&channel_lock) == 0) {
                    memory_request(&seed);
                    pthread_mutex_unlock(&channel_lock);
                    uint64_t t2 = now_ns();
                    uint64_t lat = t2 - pkt_start;

                    inc_stat(&success_count);
                    record_latency(lat);
                    log_attempt(tid, pkt_start - g_start_ns, 1, lat);
                    sent_in_round++;
                    pkt_sent = 1;
                } else {
                    inc_stat(&retry_count);
                    inc_stat(&backoff_count);
                    log_attempt(tid, t1 - g_start_ns, 0, 0);

                    usleep(rand_r(&seed) % backoff_window_us);
                    if (backoff_window_us < 2000) {
                        backoff_window_us *= 2;
                    }

                    cwnd = cwnd / 2;
                    if (cwnd < 1) cwnd = 1;
                    round_failed = 1;
                    break; // 退出当前轮
                }
            }
            if (round_failed) break;
        }

        if (!round_failed && sent_in_round == cwnd) {
            if (cwnd < cwnd_max) {
                cwnd += 1;
            }
            backoff_window_us = 20;
        }

        if (!stop_flag) {
            load_sleep(arg->load, &seed);
        }
    }
}

static void *worker(void *p) {
    worker_arg_t *arg = (worker_arg_t *)p;

    if (arg->mode == 0) {
        random_mode(arg);
    } else if (arg->mode == 1) {
        csma_mode(arg);
    } else {
        aimd_mode(arg);
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
    if (lat_count <= 0) return 0;
    int idx = (int)((p / 100.0) * (lat_count - 1));
    if (idx < 0) idx = 0;
    if (idx >= lat_count) idx = lat_count - 1;
    return latencies[idx];
}

static const char *mode_name(int mode) {
    if (mode == 0) return "random";
    if (mode == 1) return "csma";
    return "aimd";
}

int main(int argc, char **argv) {
    if (argc < 6) {
        printf("Usage: %s <mode> <load> <threads> <seconds> <seed>\n", argv[0]);
        printf("mode: 0 = Random, 1 = Basic CSMA, 2 = AIMD CSMA\n");
        return 1;
    }

    int mode = atoi(argv[1]);
    int load = atoi(argv[2]);
    int threads = atoi(argv[3]);
    int seconds = atoi(argv[4]);
    unsigned int seed = (unsigned int)atoi(argv[5]);

    if (mode < 0 || mode > 2) { printf("Error: mode must be 0,1,2\n"); return 1; }
    if (load < 1 || load > 100) { printf("Error: load 1-100\n"); return 1; }
    if (threads < 1 || threads > 64) { printf("Error: threads 1-64\n"); return 1; }
    if (seconds < 1) { printf("Error: seconds >0\n"); return 1; }

    char log_filename[256];
    snprintf(log_filename, sizeof(log_filename), "log_%s_%d_%u.txt",
             mode_name(mode), load, seed);
    g_log_file = fopen(log_filename, "w");
    if (g_log_file) {
        fprintf(g_log_file, "tid,timestamp_offset_ns,success,latency_ns\n");
    }

    mem_area = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem_area == MAP_FAILED) { perror("mmap"); return 1; }

    pthread_t *tids = malloc(sizeof(pthread_t) * threads);
    worker_arg_t *args = malloc(sizeof(worker_arg_t) * threads);
    if (!tids || !args) { printf("malloc fail\n"); return 1; }

    g_start_ns = now_ns();

    for (int i = 0; i < threads; i++) {
        args[i].tid = i;
        args[i].mode = mode;
        args[i].load = load;
        args[i].seconds = seconds;
        args[i].seed = seed + i * 17;
        pthread_create(&tids[i], NULL, worker, &args[i]);
    }

    sleep(seconds);
    stop_flag = 1;

    for (int i = 0; i < threads; i++) {
        pthread_join(tids[i], NULL);
    }

    if (g_log_file) fclose(g_log_file);

    uint64_t end = now_ns();
    double elapsed_s = (end - g_start_ns) / 1000000000.0;

    qsort(latencies, lat_count, sizeof(uint64_t), cmp_u64);
    uint64_t p50 = percentile(50.0);
    uint64_t p95 = percentile(95.0);
    uint64_t p99 = percentile(99.0);

    printf("%s,%d,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
           mode_name(mode), load, seed,
           attempts, success_count, retry_count, backoff_count,
           p50, p95, p99);

    munmap((void *)mem_area, mem_size);
    free(tids);
    free(args);
    return 0;
}