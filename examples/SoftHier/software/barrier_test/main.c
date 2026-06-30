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
    for (volatile int i = 0; i < iter; ++i) flex_global_barrier();
    return 0;
}

/**
 * @brief Test the xy barrier `flex_global_barrier_xy()`.
 * @param iter  number of iterations
 * @returns exitcode, 0 if test passed
 */
static int xy_barrier(int iter) {
    for (volatile int i = 0; i < iter; ++i) flex_global_barrier_xy();
    return 0;
}

static int group_barrier_polling(int iter) {
    static GroupMemberEncoding group_encoding = {
        .cluster_count = 4,
        .clusters = {0, 2, 5, 9}
    };
    BarrierGroup group_barrier = flex_group_barrier_init(&group_encoding);

    for (volatile int i = 0; i < iter; ++i) flex_group_barrier_polling(&group_barrier);
    return 0;
}

int main()
{
    uint32_t eoc_val = 0;
    flex_barrier_init();
    flex_barrier_xy_init();
    flex_sat(TIMEOUT);

    // Global barrier tests    
    run_test(global_barrier, 10);
    run_test(global_barrier, 100);
    run_test(global_barrier, 1000);
    
    // xy barrier tests
    run_test(xy_barrier, 10);
    run_test(xy_barrier, 100);
    run_test(xy_barrier, 1000);

    // group barrier tests
    run_test(group_barrier_polling, 10);
    run_test(group_barrier_polling, 100);
    run_test(group_barrier_polling, 1000);

    flex_eoc(eoc_val);
    return 0;
}