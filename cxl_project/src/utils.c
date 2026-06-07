#include "utils.h"

#include <stdint.h>
#include <time.h>

uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint32_t xorshift32(unsigned int *state) {
    uint32_t x = *state ? *state : 1U;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

uint64_t percentile(uint64_t *arr, uint64_t n, double p) {
    if (n == 0) return 0;
    uint64_t idx = (uint64_t)((p / 100.0) * (double)(n - 1));
    if (idx >= n) idx = n - 1;
    return arr[idx];
}
