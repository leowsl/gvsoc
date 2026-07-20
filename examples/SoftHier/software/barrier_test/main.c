#include "flex_runtime.h"
#include "flex_group_barrier.h"

/// Test Timeout in nanoseconds
#define TIMEOUT 10000000

/// Evaluates to true on exactly one core of the system
#define FIXED_CORE (flex_is_first_core() && (flex_get_cluster_id() == 0))

/// Helper to run a test function and measure the execution time
#define run_test(fn, arg) \
    do { \
        if (FIXED_CORE) printf("\n---------------------------------------------------------\n[Test Bench]: " #fn "(" #arg ")\n"); \
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


/**
 * @brief Test the global barrier `flex_global_barrier()`.
 * @param iter  number of iterations
 * @returns exitcode, 0 if test passed
 */
static int global_barrier(int iter) {
    for (int i = 0; i < iter; ++i) flex_global_barrier();
    return 0;
}

/**
 * @brief Test the xy barrier `flex_global_barrier_xy()`.
 * @param iter  number of iterations
 * @returns exitcode, 0 if test passed
 */
static int xy_barrier(int iter) {
    for (int i = 0; i < iter; ++i) flex_global_barrier_xy();
    return 0;
}

/**
 * @brief Flat group barrier, remote spinning on the leader's counter.
 */
static int group_barrier_polling(int iter) {
    uint32_t group_config[] = {0, 2, 5, 9, 14, 15};
    size_t group_config_size = sizeof(group_config) / sizeof(group_config[0]);
    flex_group_encoding group_encoding = flex_group_create_from_array(group_config, group_config_size);
    flex_group_barrier group_barrier = flex_group_barrier_init(&group_encoding);

    // Restart the timer
    if (FIXED_CORE) flex_timer_start();

    for (int i = 0; i < iter; ++i) flex_group_barrier_polling(&group_barrier);
    return 0;
}

/**
 * @brief Three disjoint groups, flat + remote spinning.
 */
static int multi_group_barrier_polling(int iter) {
    uint32_t group_config_1[] = {1, 3, 5, 7, 9, 11};
    uint32_t group_config_2[] = {0, 2, 4, 6, 8, 10};
    uint32_t group_config_3[] = {12, 13, 14, 15};
    size_t group_config_size_1 = sizeof(group_config_1) / sizeof(group_config_1[0]);
    size_t group_config_size_2 = sizeof(group_config_2) / sizeof(group_config_2[0]);
    size_t group_config_size_3 = sizeof(group_config_3) / sizeof(group_config_3[0]);
    flex_group_encoding group_encoding_1 = flex_group_create_from_array(group_config_1, group_config_size_1);
    flex_group_encoding group_encoding_2 = flex_group_create_from_array(group_config_2, group_config_size_2);
    flex_group_encoding group_encoding_3 = flex_group_create_from_array(group_config_3, group_config_size_3);

    flex_group_barrier group_barrier_1 = flex_group_barrier_init(&group_encoding_1);
    flex_group_barrier group_barrier_2 = flex_group_barrier_init(&group_encoding_2);
    flex_group_barrier group_barrier_3 = flex_group_barrier_init(&group_encoding_3);

    // Restart the timer
    if (FIXED_CORE) flex_timer_start();

    for (int i = 0; i < iter; ++i) {
        flex_group_barrier_polling(&group_barrier_1);
        flex_group_barrier_polling(&group_barrier_2);
        flex_group_barrier_polling(&group_barrier_3);
    }
    return 0;
}

/**
 * @brief Flat group barrier, LOCAL spinning (last arriver notifies all K-1 members).
 */
static int group_barrier_polling_new(int iter) {
    uint32_t group_config[] = {0, 2, 5, 9, 14, 15};
    size_t group_config_size = sizeof(group_config) / sizeof(group_config[0]);
    flex_group_encoding group_encoding = flex_group_create_from_array(group_config, group_config_size);
    flex_group_barrier group_barrier = flex_group_barrier_init(&group_encoding);

    // Restart the timer
    if (FIXED_CORE) flex_timer_start();

    for (int i = 0; i < iter; ++i) flex_group_barrier_wait(&group_barrier);
    return 0;
}

static int group_barrier_dissemination(int iter) {
    uint32_t group_config[] = {0, 2, 5, 9, 14, 15};
    size_t group_config_size = sizeof(group_config) / sizeof(group_config[0]);
    flex_group_encoding group_encoding = flex_group_create_from_array(group_config, group_config_size);
    flex_group_barrier group_barrier = flex_group_barrier_init(&group_encoding);
    dissemination_info_t d_info = dissemination_init(&group_encoding);

    // Restart the timer
    flex_global_barrier();
    if (FIXED_CORE) flex_timer_start();

    for (int i = 0; i < iter; ++i) flex_group_dissemination_barrier(&group_barrier, &d_info);
    return 0;
}

/**
 * @brief TREE group barrier: arrivals combine up, release propagates down.
 *        No cluster sees more than FANOUT messages, all spinning is local.
 * @param iter  number of iterations
 * @returns exitcode, 0 if test passed
 */
static int group_barrier_tree(int iter) {
    uint32_t group_config[] = {0, 2, 5, 9, 14, 15};
    size_t group_config_size = sizeof(group_config) / sizeof(group_config[0]);
    flex_group_encoding group_encoding = flex_group_create_from_array(group_config, group_config_size);
    flex_group_tree_barrier group_barrier = flex_group_barrier_tree_init(&group_encoding);

    // Restart the timer
    if (FIXED_CORE) flex_timer_start();

    for (volatile int i = 0; i < iter; ++i) flex_group_barrier_tree(&group_barrier);
    return 0;
}

/**
 * @brief Three disjoint groups, each with its own tree.
 *        Groups are mutually exclusive, so every cluster hosts exactly one node.
 * @param iter  number of iterations
 * @returns exitcode, 0 if test passed
 */
static int multi_group_barrier_tree(int iter) {
    uint32_t group_config_1[] = {1, 3, 5, 7, 9, 11};
    uint32_t group_config_2[] = {0, 2, 4, 6, 8, 10};
    uint32_t group_config_3[] = {12, 13, 14, 15};
    size_t group_config_size_1 = sizeof(group_config_1) / sizeof(group_config_1[0]);
    size_t group_config_size_2 = sizeof(group_config_2) / sizeof(group_config_2[0]);
    size_t group_config_size_3 = sizeof(group_config_3) / sizeof(group_config_3[0]);
    flex_group_encoding group_encoding_1 = flex_group_create_from_array(group_config_1, group_config_size_1);
    flex_group_encoding group_encoding_2 = flex_group_create_from_array(group_config_2, group_config_size_2);
    flex_group_encoding group_encoding_3 = flex_group_create_from_array(group_config_3, group_config_size_3);

    flex_group_tree_barrier group_barrier_1 = flex_group_barrier_tree_init(&group_encoding_1);
    flex_group_tree_barrier group_barrier_2 = flex_group_barrier_tree_init(&group_encoding_2);
    flex_group_tree_barrier group_barrier_3 = flex_group_barrier_tree_init(&group_encoding_3);

    // Restart the timer
    if (FIXED_CORE) flex_timer_start();

    for (volatile int i = 0; i < iter; ++i) {
        flex_group_barrier_tree(&group_barrier_1);
        flex_group_barrier_tree(&group_barrier_2);
        flex_group_barrier_tree(&group_barrier_3);
    }
    return 0;
}


int main()
{
    uint32_t eoc_val = 0;
    flex_barrier_init();
    flex_barrier_xy_init();
    flex_sat(TIMEOUT);

    // // Global barrier tests
    // run_test(global_barrier, 10);
    // run_test(global_barrier, 100);
    // run_test(global_barrier, 1000);

    // // xy barrier tests
    // run_test(xy_barrier, 10);
    // run_test(xy_barrier, 100);
    // run_test(xy_barrier, 1000);

    // // group barrier tests (flat, remote spin)
    // run_test(group_barrier_polling, 10);
    // run_test(group_barrier_polling, 100);
    // run_test(group_barrier_polling, 1000);

    // run_test(multi_group_barrier_polling, 10);
    // run_test(multi_group_barrier_polling, 100);
    // run_test(multi_group_barrier_polling, 1000);

    // // group barrier tests (flat, local spin)
    // run_test(group_barrier_polling_new, 10);
    // run_test(group_barrier_polling_new, 100);
    // run_test(group_barrier_polling_new, 1000);

    run_test(group_barrier_tree, 10);
    run_test(group_barrier_tree, 100);
    run_test(group_barrier_tree, 1000);

    run_test(multi_group_barrier_tree, 10);
    run_test(multi_group_barrier_tree, 100);
    run_test(multi_group_barrier_tree, 1000);

    run_test(group_barrier_dissemination, 10);
    run_test(group_barrier_dissemination, 100);
    run_test(group_barrier_dissemination, 1000);

    flex_eoc(eoc_val);
    return 0;
}