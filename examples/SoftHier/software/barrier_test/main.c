#include "flex_runtime.h"
#include "flex_group_barrier.h"

#include <stdlib.h>
#include <stdbool.h>

/// Test Timeout in nanoseconds
#define TIMEOUT 10000000

/// Evaluates to true on exactly one core of the system
#define FIXED_CORE (flex_is_first_core() && (flex_get_cluster_id() == 0))

/// Pseudo random number generator
unsigned long prng_state = 0;

/// RAND_MAX assumed to be 32767
int rand(void)
{
    prng_state = prng_state * 1103515245 + 12345;
    return (unsigned)(prng_state/65536) % 32768;
}

/// Create m random, disjoint groups with a specified number of clusters
flex_group_encoding * create_groups(flex_group_encoding * groups, unsigned m, unsigned size) {
    bool clusters[ARCH_NUM_CLUSTER] = { false };

    if (groups == NULL) return NULL;
    if (m * size > ARCH_NUM_CLUSTER) return NULL;

    for (unsigned group = 0; group < m; group++) {
        groups[group] = flex_group_create_zero_mask();
        for (unsigned i = 0; i < size; i++) {
            uint32_t cid;
            do {
                cid = (uint32_t) rand() % ARCH_NUM_CLUSTER;
            } while(clusters[cid] == true);
            clusters[cid] = true;
            flex_group_set_cluster(&groups[group], cid);
        }
    }

    return groups;
}

/// Helper to run a test function and measure the execution time
#define run_test(fn, arg) \
    do { \
        if (FIXED_CORE) printf("\n---------------------------------------------------------\n[Test Bench]: " #fn "\n"); \
        flex_global_barrier(); \
        if (FIXED_CORE) flex_timer_start(); \
        fn(arg); \
        flex_global_barrier(); \
        if (FIXED_CORE) { \
            flex_timer_end(); \
            printf("---------------------------------------------------------\n\n"); \
        } \
        flex_global_barrier(); \
    } while(0) \


/// Test config
struct test_config {
    int iterations;
    unsigned n_groups;
    unsigned group_size;
    flex_group_encoding *groups;
};


/// Helper to print the test config
#define TEST_PREAMBLE(config) if (FIXED_CORE) printf("\n---------------------------------------------------------\n[Config]:\nIterations %d\nN Groups   %d\nGroup Size %d\n---------------------------------------------------------\n\n", config.iterations, config.n_groups, config.group_size);


static int global_barrier(struct test_config config) {
    for (int i = 0; i < config.iterations; ++i) flex_global_barrier();
    return 0;
}


static int xy_barrier(struct test_config config) {
    for (int i = 0; i < config.iterations; ++i) flex_global_barrier_xy();
    return 0;
}


static int group_barrier_polling(struct test_config config) {
    // Initialize barriers
    flex_group_barrier barriers[config.n_groups];
    for (unsigned i = 0; i < config.n_groups; i++) {
        barriers[i] = flex_group_barrier_init(&config.groups[i]);
    }

    // Restart the timer
    flex_global_barrier();
    if (FIXED_CORE) flex_timer_start();

    for (int i = 0; i < config.iterations; ++i) {
        for (int n = 0; n < config.n_groups; ++n) {
            flex_group_barrier_polling(&barriers[n]);
        }
    }

    return 0;
}


static int group_barrier_polling_new(struct test_config config) {
    // Initialize barriers
    flex_group_barrier barriers[config.n_groups];
    for (unsigned i = 0; i < config.n_groups; i++) {
        barriers[i] = flex_group_barrier_init(&config.groups[i]);
    }

    // Restart the timer
    flex_global_barrier();
    if (FIXED_CORE) flex_timer_start();

    for (int i = 0; i < config.iterations; ++i) {
        for (int n = 0; n < config.n_groups; ++n) {
            flex_group_barrier_wait(&barriers[n]);
        }
    }

    return 0;
}


static int group_barrier_dissemination(struct test_config config) {
    // Initialize barriers
    flex_group_barrier barriers[config.n_groups];
    dissemination_info_t d_infos[config.n_groups];
    for (unsigned i = 0; i < config.n_groups; i++) {
        barriers[i] = flex_group_barrier_init(&config.groups[i]);
        d_infos[i] = dissemination_init(&config.groups[i]);
    }

    // Restart the timer
    flex_global_barrier();
    if (FIXED_CORE) flex_timer_start();

    for (int i = 0; i < config.iterations; ++i) {
        for (int n = 0; n < config.n_groups; ++n) {
            flex_group_dissemination_barrier(&barriers[n], &d_infos[n]);
        }
    }

    return 0;
}


static int group_barrier_tree(struct test_config config) {
    // Initialize barriers
    flex_group_tree_barrier barriers[config.n_groups];
    for (unsigned i = 0; i < config.n_groups; i++) {
        barriers[i] = flex_group_barrier_tree_init(&config.groups[i]);
    }

    // Restart the timer
    flex_global_barrier();
    if (FIXED_CORE) flex_timer_start();

    for (int i = 0; i < config.iterations; ++i) {
        for (int n = 0; n < config.n_groups; ++n) {
            flex_group_barrier_tree(&barriers[n]);
        }
    }
    return 0;
}


int main()
{
    uint32_t eoc_val = 0;
    flex_barrier_init();
    flex_barrier_xy_init();
    flex_sat(TIMEOUT);

    flex_group_encoding groups[2];
    struct test_config config = {
        .iterations = 1000,
        .groups = groups,
        .n_groups = 2,
        .group_size = 5,
    };
    create_groups(config.groups, config.n_groups, config.group_size);

    TEST_PREAMBLE(config)

    run_test(global_barrier, config);
    run_test(xy_barrier, config);
    run_test(group_barrier_polling, config);
    run_test(group_barrier_polling_new, config);
    run_test(group_barrier_dissemination, config);
    run_test(group_barrier_tree, config);

    flex_eoc(eoc_val);
    return 0;
}