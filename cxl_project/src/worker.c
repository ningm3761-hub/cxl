#include "worker.h"
#include "utils.h"

#include <pthread.h>
#include <unistd.h>

pthread_mutex_t channel_lock = PTHREAD_MUTEX_INITIALIZER;
volatile int stop_flag = 0;

static void load_sleep(int load, unsigned int *rng) {
    if (load < 100) {
        int idle_us = (100 - load) * 20;
        if (idle_us > 0) {
            usleep(xorshift32(rng) % (unsigned int)idle_us);
        }
    }
}

static void run_random(worker_arg_t *arg) {
    const config_t *cfg = arg->cfg;

    while (!stop_flag) {
        load_sleep(cfg->load, &arg->rng);

        arg->stats.attempts++;

        uint64_t t1 = now_ns();
        pthread_mutex_lock(&channel_lock);
        memory_region_request(arg->region, &arg->rng, cfg->touches_per_req);
        pthread_mutex_unlock(&channel_lock);
        uint64_t t2 = now_ns();

        arg->stats.success++;
        thread_stats_record_latency(&arg->stats, t2 - t1);
    }
}

static void run_csma(worker_arg_t *arg) {
    const config_t *cfg = arg->cfg;
    int backoff_window_us = 20;
    const int backoff_max_us = 2000;

    while (!stop_flag) {
        load_sleep(cfg->load, &arg->rng);

        arg->stats.attempts++;

        uint64_t t1 = now_ns();

        if (pthread_mutex_trylock(&channel_lock) == 0) {
            memory_region_request(arg->region, &arg->rng, cfg->touches_per_req);
            pthread_mutex_unlock(&channel_lock);

            uint64_t t2 = now_ns();

            arg->stats.success++;
            thread_stats_record_latency(&arg->stats, t2 - t1);
            backoff_window_us = 20;
        } else {
            arg->stats.retry++;
            arg->stats.backoff++;

            usleep(xorshift32(&arg->rng) % (unsigned int)backoff_window_us);

            if (backoff_window_us < backoff_max_us) {
                backoff_window_us *= 2;
                if (backoff_window_us > backoff_max_us) {
                    backoff_window_us = backoff_max_us;
                }
            }
        }
    }
}

static void run_aimd(worker_arg_t *arg) {
    const config_t *cfg = arg->cfg;
    int cwnd = 1;
    const int cwnd_max = 32;
    int backoff_window_us = 20;
    const int backoff_max_us = 2000;

    while (!stop_flag) {
        load_sleep(cfg->load, &arg->rng);
        int sent = 0;

        for (int i = 0; i < cwnd && !stop_flag; i++) {
            arg->stats.attempts++;
            uint64_t t1 = now_ns();

            if (pthread_mutex_trylock(&channel_lock) == 0) {
                memory_region_request(arg->region, &arg->rng, cfg->touches_per_req);
                pthread_mutex_unlock(&channel_lock);

                uint64_t t2 = now_ns();

                arg->stats.success++;
                thread_stats_record_latency(&arg->stats, t2 - t1);
                sent++;
            } else {
                arg->stats.retry++;
                arg->stats.backoff++;

                cwnd /= 2;
                if (cwnd < 1) cwnd = 1;

                usleep(xorshift32(&arg->rng) % (unsigned int)backoff_window_us);

                if (backoff_window_us < backoff_max_us) {
                    backoff_window_us *= 2;
                    if (backoff_window_us > backoff_max_us) {
                        backoff_window_us = backoff_max_us;
                    }
                }

                break;
            }
        }

        if (sent == cwnd) {
            if (cwnd < cwnd_max) {
                cwnd++;
            }
            backoff_window_us = 20;
        }
    }
}

void *worker_main(void *p) {
    worker_arg_t *arg = (worker_arg_t *)p;

    bind_current_thread_to_node(arg->cfg->cpu_node);

    if (arg->cfg->mode == 0) {
        run_random(arg);
    } else if (arg->cfg->mode == 1) {
        run_csma(arg);
    } else {
        run_aimd(arg);
    }

    return NULL;
}
