#include "config.h"
#include "numa_backend.h"
#include "stats.h"
#include "utils.h"
#include "worker.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    config_t cfg;

    if (parse_config(argc, argv, &cfg) != 0) {
        return 1;
    }

    if (!numa_backend_available()) {
        fprintf(stderr, "Error: NUMA is not available. Check QEMU NUMA configuration.\n");
        return 1;
    }

    if (!numa_node_exists(cfg.mem_node)) {
        fprintf(stderr, "Error: mem_node=%d does not exist. Check `numactl -H`.\n", cfg.mem_node);
        return 1;
    }

    print_config_stderr(&cfg);

    memory_region_t region;
    if (memory_region_init(&region, cfg.mem_size, cfg.mem_node) != 0) {
        return 1;
    }

    pthread_t *tids = calloc((size_t)cfg.threads, sizeof(pthread_t));
    worker_arg_t *args = calloc((size_t)cfg.threads, sizeof(worker_arg_t));
    if (!tids || !args) {
        fprintf(stderr, "Error: calloc failed\n");
        memory_region_destroy(&region);
        free(tids);
        free(args);
        return 1;
    }

    for (int i = 0; i < cfg.threads; i++) {
        args[i].tid = i;
        args[i].cfg = &cfg;
        args[i].region = &region;
        args[i].rng = cfg.seed + (unsigned int)i * 10007U;

        if (thread_stats_init(&args[i].stats, MAX_LAT_PER_THREAD) != 0) {
            fprintf(stderr, "Error: latency buffer allocation failed\n");
            memory_region_destroy(&region);
            free(tids);
            free(args);
            return 1;
        }
    }

    uint64_t start_ns = now_ns();

    for (int i = 0; i < cfg.threads; i++) {
        int rc = pthread_create(&tids[i], NULL, worker_main, &args[i]);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_create failed: %s\n", strerror(rc));
            stop_flag = 1;
            return 1;
        }
    }

    sleep(cfg.seconds);
    stop_flag = 1;

    for (int i = 0; i < cfg.threads; i++) {
        pthread_join(tids[i], NULL);
    }

    uint64_t end_ns = now_ns();
    double elapsed_s = (double)(end_ns - start_ns) / 1000000000.0;

    summary_stats_t sum;
    if (summarize_stats(&args[0].stats, cfg.threads, elapsed_s, &sum) != 0) {
        fprintf(stderr, "Error: summarize_stats failed\n");
        return 1;
    }

    printf("track,load,seed,attempts,success,retry,backoff,delay_p50,delay_p95,delay_p99,goodput,threads,seconds,mem_node,cpu_node,mem_mb,touches_per_req,latency_samples,backend\n");
    printf("%s,%d,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.2f,%d,%d,%d,%d,%d,%d,%" PRIu64 ",numa_node%d\n",
           mode_name(cfg.mode),
           cfg.load,
           cfg.seed,
           sum.attempts,
           sum.success,
           sum.retry,
           sum.backoff,
           sum.p50,
           sum.p95,
           sum.p99,
           sum.goodput,
           cfg.threads,
           cfg.seconds,
           cfg.mem_node,
           cfg.cpu_node,
           cfg.mem_mb,
           cfg.touches_per_req,
           sum.latency_count,
           cfg.mem_node);

    for (int i = 0; i < cfg.threads; i++) {
        thread_stats_destroy(&args[i].stats);
    }

    memory_region_destroy(&region);
    free(tids);
    free(args);

    return 0;
}
