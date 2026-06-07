#ifndef CXL_NUMA_CSMA_CONFIG_H
#define CXL_NUMA_CSMA_CONFIG_H

#include <stddef.h>

typedef struct {
    int mode;
    int load;
    int threads;
    int seconds;
    int mem_node;
    int cpu_node;
    unsigned int seed;
    size_t mem_size;
    int mem_mb;
    int touches_per_req;
} config_t;

const char *mode_name(int mode);
int parse_config(int argc, char **argv, config_t *cfg);
void print_usage(const char *prog);
void print_config_stderr(const config_t *cfg);

#endif
