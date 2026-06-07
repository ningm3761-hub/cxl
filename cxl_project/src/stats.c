#include "stats.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

int thread_stats_init(thread_stats_t *s, uint64_t latency_capacity) {
    memset(s, 0, sizeof(*s));
    s->latency_capacity = latency_capacity;
    s->latencies = calloc(latency_capacity, sizeof(uint64_t));
    return s->latencies ? 0 : -1;
}

void thread_stats_destroy(thread_stats_t *s) {
    free(s->latencies);
    s->latencies = NULL;
    s->latency_capacity = 0;
    s->latency_count = 0;
}

void thread_stats_record_latency(thread_stats_t *s, uint64_t ns) {
    if (s->latency_count < s->latency_capacity) {
        s->latencies[s->latency_count++] = ns;
    }
}

int summarize_stats(thread_stats_t *stats, int nthreads, double elapsed_s, summary_stats_t *out) {
    memset(out, 0, sizeof(*out));

    for (int i = 0; i < nthreads; i++) {
        out->attempts += stats[i].attempts;
        out->success += stats[i].success;
        out->retry += stats[i].retry;
        out->backoff += stats[i].backoff;
        out->latency_count += stats[i].latency_count;
    }

    uint64_t *all = NULL;
    if (out->latency_count > 0) {
        all = malloc(sizeof(uint64_t) * out->latency_count);
        if (!all) {
            return -1;
        }

        uint64_t pos = 0;
        for (int i = 0; i < nthreads; i++) {
            memcpy(&all[pos], stats[i].latencies, sizeof(uint64_t) * stats[i].latency_count);
            pos += stats[i].latency_count;
        }

        qsort(all, out->latency_count, sizeof(uint64_t), cmp_u64);
        out->p50 = percentile(all, out->latency_count, 50.0);
        out->p95 = percentile(all, out->latency_count, 95.0);
        out->p99 = percentile(all, out->latency_count, 99.0);
        free(all);
    }

    out->goodput = elapsed_s > 0.0 ? (double)out->success / elapsed_s : 0.0;
    return 0;
}
