#define _GNU_SOURCE

#include "numa_backend.h"
#include "utils.h"

#include <errno.h>
#include <numa.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHELINE_SIZE 64

int numa_backend_available(void) {
    return numa_available() >= 0;
}

int numa_node_exists(int node) {
    if (node < 0 || numa_available() < 0) return 0;
    return numa_bitmask_isbitset(numa_all_nodes_ptr, node);
}

int bind_current_thread_to_node(int node) {
    if (!numa_node_exists(node)) {
        fprintf(stderr, "Warning: cpu_node=%d does not exist; skip CPU binding\n", node);
        return -1;
    }

    struct bitmask *cpus = numa_allocate_cpumask();
    if (!cpus) {
        perror("numa_allocate_cpumask");
        return -1;
    }

    if (numa_node_to_cpus(node, cpus) != 0) {
        fprintf(stderr, "Warning: numa_node_to_cpus(%d) failed: %s\n", node, strerror(errno));
        numa_free_cpumask(cpus);
        return -1;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    int added = 0;

    for (unsigned int i = 0; i < cpus->size && i < CPU_SETSIZE; i++) {
        if (numa_bitmask_isbitset(cpus, i)) {
            CPU_SET(i, &set);
            added++;
        }
    }

    numa_free_cpumask(cpus);

    if (added == 0) {
        fprintf(stderr, "Warning: node %d has no CPU; skip CPU binding\n", node);
        return -1;
    }

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        fprintf(stderr, "Warning: sched_setaffinity(node=%d) failed: %s\n",
                node, strerror(errno));
        return -1;
    }

    return 0;
}

void *alloc_memory_on_node(size_t size, int mem_node) {
    if (!numa_backend_available()) {
        fprintf(stderr, "Error: NUMA is not available\n");
        return NULL;
    }

    if (!numa_node_exists(mem_node)) {
        fprintf(stderr, "Error: mem_node=%d does not exist. Check `numactl -H`.\n", mem_node);
        return NULL;
    }

    numa_set_strict(1);

    void *p = numa_alloc_onnode(size, mem_node);
    if (!p) {
        fprintf(stderr, "Error: numa_alloc_onnode(size=%zu,node=%d) failed\n", size, mem_node);
        return NULL;
    }

    numa_tonode_memory(p, size, mem_node);
    memset(p, 0, size);
    return p;
}

void free_memory_on_node(void *ptr, size_t size) {
    if (ptr) {
        numa_free(ptr, size);
    }
}

int memory_region_init(memory_region_t *region, size_t size, int mem_node) {
    region->base = (volatile uint8_t *)alloc_memory_on_node(size, mem_node);
    if (!region->base) {
        return -1;
    }

    region->size = size;
    region->mem_node = mem_node;
    return 0;
}

void memory_region_destroy(memory_region_t *region) {
    if (region && region->base) {
        free_memory_on_node((void *)region->base, region->size);
        region->base = NULL;
        region->size = 0;
    }
}

void memory_region_request(memory_region_t *region, unsigned int *rng, int touches_per_req) {
    size_t lines = region->size / CACHELINE_SIZE;
    if (lines == 0) return;

    for (int i = 0; i < touches_per_req; i++) {
        size_t line = (size_t)(xorshift32(rng) % lines);
        size_t off = line * CACHELINE_SIZE;

        uint8_t v = region->base[off];
        region->base[off] = (uint8_t)(v + 1);
    }
}
