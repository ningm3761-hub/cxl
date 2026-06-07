#ifndef CXL_NUMA_CSMA_NUMA_BACKEND_H
#define CXL_NUMA_CSMA_NUMA_BACKEND_H

#include <stddef.h>
#include <stdint.h>

int numa_backend_available(void);
int numa_node_exists(int node);
int bind_current_thread_to_node(int node);

void *alloc_memory_on_node(size_t size, int mem_node);
void free_memory_on_node(void *ptr, size_t size);

typedef struct {
    volatile uint8_t *base;
    size_t size;
    int mem_node;
} memory_region_t;

int memory_region_init(memory_region_t *region, size_t size, int mem_node);
void memory_region_destroy(memory_region_t *region);
void memory_region_request(memory_region_t *region, unsigned int *rng, int touches_per_req);

#endif
