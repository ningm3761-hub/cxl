#ifndef CXL_NUMA_CSMA_WORKER_H
#define CXL_NUMA_CSMA_WORKER_H

#include "config.h"
#include "numa_backend.h"
#include "stats.h"

#include <pthread.h>

#define MAX_LAT_PER_THREAD 200000

typedef struct {
    int tid;
    const config_t *cfg;
    memory_region_t *region;
    unsigned int rng;
    thread_stats_t stats;
} worker_arg_t;

extern pthread_mutex_t channel_lock;
extern volatile int stop_flag;

void *worker_main(void *p);

#endif
