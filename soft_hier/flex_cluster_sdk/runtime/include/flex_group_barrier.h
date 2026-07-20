#ifndef _FLEX_GROUP_BARRIER_H_
#define _FLEX_GROUP_BARRIER_H_

#include <stdbool.h>
#include "flex_runtime.h"
#include "flex_cluster_arch.h"

/*****************************************
*  Grid Group Synchronization functions  *
*****************************************/

typedef struct GridSyncGroupInfo
{
    //General information
    uint32_t valid_grid;
    uint32_t grid_x_dim;
    uint32_t grid_y_dim;
    uint32_t grid_x_num;
    uint32_t grid_y_num;

    //Local information
    uint32_t this_grid_id;
    uint32_t this_grid_id_x;
    uint32_t this_grid_id_y;
    uint32_t this_grid_left_most;
    uint32_t this_grid_right_most;
    uint32_t this_grid_top_most;
    uint32_t this_grid_bottom_most;
    uint32_t this_grid_cluster_num;
    uint32_t this_grid_cluster_num_x;
    uint32_t this_grid_cluster_num_y;

    //Sync information
    uint8_t  wakeup_row_mask;
    uint8_t  wakeup_col_mask;
    uint32_t sync_x_cluster;
    uint32_t sync_y_cluster;
    volatile uint32_t * sync_x_point;
    volatile uint32_t * sync_x_piter;
    volatile uint32_t * sync_y_point;
    volatile uint32_t * sync_y_piter;
}GridSyncGroupInfo;

GridSyncGroupInfo grid_sync_group_init(uint32_t grid_x_dim, uint32_t grid_y_dim){
	GridSyncGroupInfo info;

	//Calculate information
	info.valid_grid = 1;
	info.grid_x_dim = grid_x_dim;
	info.grid_y_dim = grid_y_dim;

	if (grid_x_dim == 0 || grid_y_dim == 0)
	{
		info.valid_grid = 0;
		return info;
	}

    if (!(is_power_of_two(grid_x_dim) && is_power_of_two(grid_y_dim)))
    {
        info.valid_grid = 0;
        return info;
    }

	info.grid_x_num = (ARCH_NUM_CLUSTER_X + grid_x_dim - 1)/grid_x_dim;
	info.grid_y_num = (ARCH_NUM_CLUSTER_Y + grid_y_dim - 1)/grid_y_dim;

	FlexPosition pos = get_pos(flex_get_cluster_id());
	info.this_grid_id_x = pos.x/grid_x_dim;
	info.this_grid_id_y = pos.y/grid_y_dim;
	info.this_grid_id   = info.grid_x_num * info.this_grid_id_y + info.this_grid_id_x;
	info.this_grid_left_most = info.this_grid_id_x * grid_x_dim;
	info.this_grid_right_most = (info.this_grid_id_x + 1) * grid_x_dim - 1;
	info.this_grid_right_most = info.this_grid_right_most >= ARCH_NUM_CLUSTER_X? ARCH_NUM_CLUSTER_X - 1 : info.this_grid_right_most;
	info.this_grid_bottom_most = info.this_grid_id_y * grid_y_dim;
	info.this_grid_top_most = (info.this_grid_id_y + 1) * grid_y_dim - 1;
	info.this_grid_top_most = info.this_grid_top_most >= ARCH_NUM_CLUSTER_Y? ARCH_NUM_CLUSTER_Y - 1 : info.this_grid_top_most;
	info.this_grid_cluster_num_x = info.this_grid_right_most + 1 - info.this_grid_left_most;
	info.this_grid_cluster_num_y = info.this_grid_top_most + 1 - info.this_grid_bottom_most;
    info.this_grid_cluster_num   = info.this_grid_cluster_num_x * info.this_grid_cluster_num_y;

    info.wakeup_row_mask= ~(info.grid_x_dim - 1);
    info.wakeup_col_mask= ~(info.grid_y_dim - 1);
	info.sync_x_cluster = (info.this_grid_left_most + info.this_grid_right_most)/2;
	info.sync_y_cluster = (info.this_grid_bottom_most + info.this_grid_top_most)/2;
	info.sync_x_point   = (volatile uint32_t *) (ARCH_SYNC_BASE+(cluster_index(info.sync_x_cluster,pos.y              )*ARCH_SYNC_SIZE)+24);
    info.sync_x_piter   = (volatile uint32_t *) (ARCH_SYNC_BASE+(cluster_index(info.sync_x_cluster,pos.y              )*ARCH_SYNC_SIZE)+28);
    info.sync_y_point   = (volatile uint32_t *) (ARCH_SYNC_BASE+(cluster_index(info.sync_x_cluster,info.sync_y_cluster)*ARCH_SYNC_SIZE)+32);
    info.sync_y_piter   = (volatile uint32_t *) (ARCH_SYNC_BASE+(cluster_index(info.sync_x_cluster,info.sync_y_cluster)*ARCH_SYNC_SIZE)+36);

	if (flex_get_core_id() == 0)
	{
		//Reset synchronization point
		volatile uint32_t * local_sync_point_for_group_level1 = (volatile uint32_t *) (ARCH_SYNC_BASE+(flex_get_cluster_id()*ARCH_SYNC_SIZE)+24);
		volatile uint32_t * local_sync_point_for_group_level2 = (volatile uint32_t *) (ARCH_SYNC_BASE+(flex_get_cluster_id()*ARCH_SYNC_SIZE)+32);
		*local_sync_point_for_group_level1 = 0;
		*local_sync_point_for_group_level2 = 0;
	}

	return info;
}


void grid_sync_group_barrier_xy(GridSyncGroupInfo * info){

    flex_intra_cluster_sync();

    if (flex_is_dm_core()){
        flex_annotate_barrier(0);

    	volatile uint32_t * cluster_wfi_reg  = (volatile uint32_t *) ARCH_CLUSTER_REG_BASE;

        //First Barrier X
        if ((info->this_grid_cluster_num_x - flex_get_enable_value()) == flex_amo_fetch_add(info->sync_x_point)) {
            flex_reset_barrier(info->sync_x_point);

            //For cluster synced X, then sync Y
            if ((info->this_grid_cluster_num_y - flex_get_enable_value()) == flex_amo_fetch_add(info->sync_y_point))
            {
                flex_reset_barrier(info->sync_y_point);
                flex_wakeup_clusters(info->wakeup_row_mask,info->wakeup_col_mask);
            }
        }
        *cluster_wfi_reg = flex_get_enable_value();

        flex_annotate_barrier(0);
    }

    flex_intra_cluster_sync();
}

void grid_sync_group_barrier_xy_polling(GridSyncGroupInfo * info){

    flex_intra_cluster_sync();

    if (flex_is_dm_core()){
        flex_annotate_barrier(0);

        // Remember previous iteration
        uint32_t prev_barrier_iter_x     = *(info->sync_x_piter);
        uint32_t prev_barrier_iter_y     = *(info->sync_y_piter);

        //First Barrier X
        if ((info->this_grid_cluster_num_x - flex_get_enable_value()) == flex_amo_fetch_add(info->sync_x_point)) {
            flex_reset_barrier(info->sync_x_point);

            //For cluster synced X, then sync Y
            if ((info->this_grid_cluster_num_y - flex_get_enable_value()) == flex_amo_fetch_add(info->sync_y_point))
            {
                flex_reset_barrier(info->sync_y_point);
                flex_amo_fetch_add(info->sync_y_piter);
            } else {
                while((*(info->sync_y_piter)) == prev_barrier_iter_y);
            }

            flex_amo_fetch_add(info->sync_x_piter);
        } else {
            while((*(info->sync_x_piter)) == prev_barrier_iter_x);
        }
        flex_annotate_barrier(0);
    }

    flex_intra_cluster_sync();
}


/**********************************************
*  Arbitrary Group Synchronization functions  *
**********************************************/

#define FLEX_GROUP_SYNC_COUNTER_REG   40      /// Offset for the counter register in the ARCH_SYNC region.
#define FLEX_GROUP_SYNC_PARITY_BIT    31      /// Parity bit inside the counter that gets flipped when the counter wraps around
#define FLEX_GROUP_SYNC_PARITY_MASK   (1 << FLEX_GROUP_SYNC_PARITY_BIT)
#define FLEX_GROUP_SYNC_COUNTER_MASK ~FLEX_GROUP_SYNC_PARITY_MASK
#define FLEX_GROUP_CLUSTER_WORDS      ((ARCH_NUM_CLUSTER + 31) / 32)    /// Number of words to represent all clusters as bits
#define FLEX_GROUP_SLOTS_PER_CLUSTER  1     /// Number of counter slots per cluster (1 = single counter per cluster, can be 10 or more)
#define FLEX_GROUP_NUM_GROUP_SLOTS    (ARCH_NUM_CLUSTER * FLEX_GROUP_SLOTS_PER_CLUSTER)     /// Total Number of available counter slots


/// Encoding of an arbitrary group of clusters
typedef struct {
    uint32_t mask[FLEX_GROUP_CLUSTER_WORDS];
} flex_group_encoding;


/// A barrier for an arbitrary group of clusters
typedef struct {
    const flex_group_encoding * group_encoding;
    volatile uint32_t * sync_register;
    bool contains_me;
    size_t cluster_count;
} flex_group_barrier;


void * get_flex_group_register(uint32_t cid) {    
    if (cid < 0 || cid >= ARCH_NUM_CLUSTER) {
        return NULL;
    }
    void * cluster_addr = ((void *) ARCH_SYNC_BASE) + (ARCH_SYNC_INTERLEAVE + ARCH_SYNC_SPECIAL_MEM) * cid;
    return cluster_addr + FLEX_GROUP_SYNC_COUNTER_REG;
}


uint32_t get_flex_group_leader(const flex_group_encoding * group_encoding) {
    for (int cid = 0; cid < ARCH_NUM_CLUSTER; cid++) {
        int word = cid / 32;
        int bit = cid % 32;
        if ((group_encoding->mask[word] >> bit) == 0x1) return cid;
    }
    return 0xFFFFFFFF;
}


flex_group_encoding flex_group_create_zero_mask() {
    flex_group_encoding group_encoding = { .mask = 0 };
    return group_encoding;
}


void flex_group_set_cluster(flex_group_encoding * group_encoding, uint32_t cid) {
    if (cid >= ARCH_NUM_CLUSTER) return;

    int word = cid / 32;
    int bit = cid % 32;
    group_encoding->mask[word] |= (1u << (bit));
}


flex_group_encoding flex_group_create_from_array(uint32_t clusters[], size_t len) {
    flex_group_encoding group_encoding = flex_group_create_zero_mask();

    for (int i = 0; i < len; i++) {
        flex_group_set_cluster(&group_encoding, clusters[i]);
    }

    return group_encoding;
}

bool flex_group_contains_cluster(const flex_group_encoding * group_encoding, uint32_t cid) {
    int word = cid / 32;
    int bit = cid % 32;
    return (group_encoding->mask[word] >> bit) & 0x1;
}


bool flex_group_contains_me(const flex_group_encoding * group_encoding) {
    return flex_group_contains_cluster(group_encoding, flex_get_cluster_id());
}


uint32_t flex_group_get_cluster_cnt(const flex_group_encoding * group_encoding) {
    uint32_t group_ctr = 0;

    for (int cid = 0; cid < ARCH_NUM_CLUSTER; cid++) {
        int word = cid / 32;
        int bit = cid % 32;
        group_ctr += ((group_encoding->mask[word] >> bit) & 0x1);
    }

    return group_ctr;
}

/**
 * @brief Initialize a barrier with the given group
 * 
 * @param group_encoding Encoding of the group
 * 
 * @returns A group barrier for the given encoding
 */
flex_group_barrier flex_group_barrier_init(const flex_group_encoding * group_encoding) {
    flex_group_barrier barrier = {
        .group_encoding = group_encoding,
        .sync_register = get_flex_group_register(get_flex_group_leader(group_encoding)),
        .contains_me = flex_group_contains_me(group_encoding),
        .cluster_count = flex_group_get_cluster_cnt(group_encoding),
    };
    
    // Every member resets their registers
	if (barrier.contains_me && flex_get_core_id() == 0) {
        flex_reset_barrier(get_flex_group_register(flex_get_cluster_id()));
    }
    flex_global_barrier();

    return barrier;
}


/**
 * @brief Wait for a group barrier.
 *        Returns only after all clusters of the group reached this statement.
 *        In contrast to `flex_group_barrier_polling`, this function spins on the local register.
 *        When the last cluster arrives at the barrier, all local registers are notified.
 * 
 * @param barrier Group barrier instance
 * 
 * @returns This function only returns after all clusters in the group reached that barrier.
 */
void flex_group_barrier_wait(const flex_group_barrier * barrier) {
    flex_intra_cluster_sync();

    if (flex_is_dm_core() && barrier->contains_me) {
        flex_annotate_barrier(0);

        // Store initial parity value & increment counter
        volatile uint32_t * local_register = get_flex_group_register(flex_get_cluster_id());
        uint32_t parity = *local_register & FLEX_GROUP_SYNC_PARITY_MASK;
        uint32_t counter = flex_amo_fetch_add(barrier->sync_register) & FLEX_GROUP_SYNC_COUNTER_MASK;

        // Counter is full, notify waiting clusters
        if (counter == (barrier->cluster_count - flex_get_enable_value())) {
            *(barrier->sync_register) = parity ^ FLEX_GROUP_SYNC_PARITY_MASK;

            // Need to loop through full mask
            for (int cid = 0; cid < ARCH_NUM_CLUSTER; cid++) {
                int word = cid / 32;
                int bit = cid % 32;
                if((barrier->group_encoding->mask[word] >> bit) & 0x1) {
                    volatile uint32_t * reg = get_flex_group_register(cid);
                    if (reg != barrier->sync_register) {
                        *reg = parity ^ FLEX_GROUP_SYNC_PARITY_MASK;
                    }
                }
            }
        }

        // Counter is not full, wait for notification
        else {
            while((*local_register & FLEX_GROUP_SYNC_PARITY_MASK) == parity);
        }
        flex_annotate_barrier(0);
    }

    flex_intra_cluster_sync();
}


/**
 * @brief Wait for a group barrier.
 *        Returns only after all clusters of the group reached this statement.
 *        In contrast to `flex_group_barrier_wait`, this function spins on the (remote) counter register.
 * 
 * @param barrier Group barrier instance
 * 
 * @returns This function only returns after all clusters in the group reached that barrier.
 */
void flex_group_barrier_polling(const flex_group_barrier * barrier) {
    flex_intra_cluster_sync();

    if (flex_is_dm_core() && barrier->contains_me) {
        flex_annotate_barrier(0);
        uint32_t counter = flex_amo_fetch_add(barrier->sync_register);
        uint32_t val     = counter & FLEX_GROUP_SYNC_COUNTER_MASK;
        uint32_t parity  = counter & FLEX_GROUP_SYNC_PARITY_MASK;

        if (val == (barrier->cluster_count - flex_get_enable_value())) {
            *(barrier->sync_register) = parity ^ FLEX_GROUP_SYNC_PARITY_MASK;
        } else {
            while((*(barrier->sync_register) & FLEX_GROUP_SYNC_PARITY_MASK) == parity);
        }
        flex_annotate_barrier(0);
    }

    flex_intra_cluster_sync();
}


/**********************************************
*  Dissemination Barrier                      *
**********************************************/

#define DISSEMINATION_MAX_ROUNDS 10
#define MAX_GROUP_SIZE (1U << DISSEMINATION_MAX_ROUNDS)

#define DISSEMINATION_ROUND_MASK(round, parity) (1 << (round + DISSEMINATION_MAX_ROUNDS * parity))
#define DISSEMINATION_RESET_MASK(parity) ~(0xFFFF << (DISSEMINATION_MAX_ROUNDS * parity))

typedef struct {
    uint8_t parity;
    uint8_t n_rounds;
    volatile uint32_t * local_reg;
    volatile uint32_t * partner_reg[DISSEMINATION_MAX_ROUNDS];
} dissemination_info_t;

bool dissemination_get_flag(volatile uint32_t * reg, uint8_t round, uint8_t parity) {
    return (*reg & DISSEMINATION_ROUND_MASK(round, parity)) != 0;
}

void dissemination_set_flag(volatile uint32_t * reg, uint8_t round, uint8_t parity) {
    flex_amo_or(reg, DISSEMINATION_ROUND_MASK(round, parity));
}

void dissemination_reset_flags(volatile uint32_t * reg, uint8_t parity) {
    flex_amo_and(reg, DISSEMINATION_RESET_MASK(parity));
}

dissemination_info_t dissemination_init(const flex_group_encoding * group_encoding) {
    dissemination_info_t d_info = {
        .parity = 0,
        .n_rounds = 0,
        .local_reg = get_flex_group_register(flex_get_cluster_id()),
        .partner_reg = { NULL }
    };
    if (!flex_group_contains_me(group_encoding)) return d_info;

    // Get a list of clusters in the group from the mask
    uint32_t group_size = flex_group_get_cluster_cnt(group_encoding);
    uint32_t group_members[group_size];
    for (
        uint32_t cid = flex_get_cluster_id(), n_members = 0;
        n_members < group_size;
        cid = (cid + 1) % ARCH_NUM_CLUSTER)
    {
        if (flex_group_contains_cluster(group_encoding, cid)) {
            group_members[n_members++] = cid;
        }
    }

    // Calculate number of rounds needed
    uint8_t n_rounds = 0;
    while(group_size > (1 << n_rounds)) n_rounds++;
    d_info.n_rounds = n_rounds;

    // Calculate partner for each round
    for (uint8_t round = 0; round < n_rounds; round++) {
        uint32_t partner = group_members[1 << round];   // As n_rounds <= log2(group_size), pow2(round) can not be out of bound
        d_info.partner_reg[round] = get_flex_group_register(partner);
    }

    return d_info;
}


void flex_group_dissemination_barrier(
    const flex_group_barrier * barrier,
    dissemination_info_t * d_info
) {
    flex_intra_cluster_sync();

    if (flex_is_dm_core() && barrier->contains_me) {
        flex_annotate_barrier(0);

        for (uint8_t round = 0; round < d_info->n_rounds; round++) {
            dissemination_set_flag(d_info->partner_reg[round], round, d_info->parity);
            while(dissemination_get_flag(d_info->local_reg, round, d_info->parity) != 0x1);
        }
        dissemination_reset_flags(d_info->local_reg, d_info->parity);
        d_info->parity ^= 0x1;

        flex_annotate_barrier(0);
    }

    flex_intra_cluster_sync();
}


#endif