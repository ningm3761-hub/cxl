#ifndef CXL_NUMA_CSMA_STATS_H
#define CXL_NUMA_CSMA_STATS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t attempts;
    uint64_t success;
    uint64_t retry;
    uint64_t backoff;
    uint64_t latency_count;
    uint64_t *latencies;
    uint64_t latency_capacity;
} thread_stats_t;

typedef struct {
    uint64_t attempts;
    uint64_t success;
    uint64_t retry;
    uint64_t backoff;
    uint64_t latency_count;
    uint64_t p50;
    uint64_t p95;
    uint64_t p99;
    double goodput;
} summary_stats_t;

int thread_stats_init(thread_stats_t *s, uint64_t latency_capacity);
void thread_stats_destroy(thread_stats_t *s);
void thread_stats_record_latency(thread_stats_t *s, uint64_t ns);

int summarize_stats(thread_stats_t *stats, int nthreads, double elapsed_s, summary_stats_t *out);

#endif
