#ifndef CXL_NUMA_CSMA_UTILS_H
#define CXL_NUMA_CSMA_UTILS_H

#include <stdint.h>

uint64_t now_ns(void);
uint32_t xorshift32(unsigned int *state);
int cmp_u64(const void *a, const void *b);
uint64_t percentile(uint64_t *arr, uint64_t n, double p);

#endif
