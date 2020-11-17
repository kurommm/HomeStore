﻿/*
 * fixed_blk_allocator.cpp
 *
 *  Created on: Aug 09, 2016
 *      Author: hkadayam
 */

#include <cassert>
#include "engine/common/homestore_flip.hpp"

#include "blk_allocator.h"

using namespace std;

namespace homestore {

FixedBlkAllocator::FixedBlkAllocator(const BlkAllocConfig& cfg, const bool init, const uint32_t id) :
        BlkAllocator(cfg, id) {
    m_blk_nodes.reset(new __fixed_blk_node[cfg.get_total_blks()]);

    if (init) { inited(); }
}

FixedBlkAllocator::~FixedBlkAllocator() {}

void FixedBlkAllocator::inited() {
    m_first_blk_id = BLKID32_INVALID;
    /* create the blkid chain */
    uint32_t prev_blkid = BLKID32_INVALID;
    for (uint32_t i = 0; i < (uint32_t)m_cfg.get_total_blks(); i++) {
#ifndef NDEBUG
        m_blk_nodes[i].this_blk_id = i;
#endif
        BlkAllocPortion* portion = blknum_to_portion(i);
        {
            auto lock{portion->portion_auto_lock()};
            if (get_disk_bm()->is_bits_set(i, 1)) { continue; }
        }
        if (m_first_blk_id == BLKID32_INVALID) { m_first_blk_id = i; }
        if (prev_blkid != BLKID32_INVALID) { m_blk_nodes[prev_blkid].next_blk = i; }
        prev_blkid = i;
    }
    HS_DEBUG_ASSERT_NE(prev_blkid, BLKID32_INVALID, "Have invalid prev_blkid");
    m_blk_nodes[prev_blkid].next_blk = BLKID32_INVALID;

    HS_DEBUG_ASSERT_NE(m_first_blk_id, BLKID32_INVALID, "Have invalid first_blk_id");
    __top_blk tp(0, m_first_blk_id);
    m_top_blk_id.store(tp.to_integer());
    BlkAllocator::inited();
}

bool FixedBlkAllocator::is_blk_alloced(const BlkId& b) const {
    /* We need to take lock so we can check in non debug builds */
    if (!m_inited) { return true; }
    return (BlkAllocator::is_blk_alloced_on_disk(b));
}

BlkAllocStatus FixedBlkAllocator::alloc(const uint8_t nblks, const blk_alloc_hints& hints,
                                        std::vector< BlkId >& out_blkid) {
    BlkId blkid;
    /* TODO:If it is more then 1 then we need to make sure that we never allocate across the portions */
    HS_DEBUG_ASSERT_EQ(nblks, 1, "FixedBlkAllocator does not support multiple blk allocation yet");

#ifdef _PRERELEASE
    if (homestore_flip->test_flip("fixed_blkalloc_no_blks", nblks)) { return BlkAllocStatus::BLK_ALLOC_SPACEFULL; }
#endif
    if (alloc(nblks, hints, &blkid) == BlkAllocStatus::BLK_ALLOC_SUCCESS) {
        out_blkid.push_back(blkid);
        return BlkAllocStatus::BLK_ALLOC_SUCCESS;
    }
    /* We don't support the vector of blkids in fixed blk allocator */
    return BlkAllocStatus::BLK_ALLOC_SPACEFULL;
}

BlkAllocStatus FixedBlkAllocator::alloc(const uint8_t nblks, const blk_alloc_hints& hints, BlkId* const out_blkid,
                                        const bool best_fit) {
    uint64_t prev_val;
    uint64_t cur_val;
    uint32_t id;

    HS_DEBUG_ASSERT_EQ(nblks, 1, "FixedBlkAllocator does not support multiple blk allocation yet");
    HS_DEBUG_ASSERT_EQ(m_inited, true, "Allocation before FixedBlkAllocator is initialized");

    do {
        prev_val = m_top_blk_id.load();
        __top_blk tp(prev_val);

        // Get the __top_blk blk and replace the __top_blk blk id with next id
        id = tp.get_top_blk_id();
        if (id == BLKID32_INVALID) { return BlkAllocStatus::BLK_ALLOC_SPACEFULL; }

        __fixed_blk_node blknode = m_blk_nodes[id];

        tp.set_top_blk_id(blknode.next_blk);
        tp.set_gen(tp.get_gen() + 1);
        cur_val = tp.to_integer();

    } while (!(m_top_blk_id.compare_exchange_weak(prev_val, cur_val)));

    out_blkid->set(id, 1, 0);

    m_alloc_blk_cnt.fetch_add(nblks, std::memory_order_relaxed);
    return BlkAllocStatus::BLK_ALLOC_SUCCESS;
}

void FixedBlkAllocator::free(const BlkId& b) {
    HS_DEBUG_ASSERT_EQ(b.get_nblks(), 1, "Multiple blk free for FixedBlkAllocator? allocated by different allocator?");

    /* No need to set in cache if it is not recovered. When recovery is complete we copy the disk_bm to
     * cache bm.
     */
    if (m_inited) {
        free_blk((uint32_t)b.get_id());
        m_alloc_blk_cnt.fetch_sub(b.get_nblks(), std::memory_order_relaxed);
    }
}

void FixedBlkAllocator::free_blk(const uint32_t id) {
    uint64_t prev_val;
    uint64_t cur_val;
    __fixed_blk_node& blknode = m_blk_nodes[id];

    do {
        prev_val = m_top_blk_id.load();
        __top_blk tp(prev_val);

        blknode.next_blk = tp.get_top_blk_id();

        tp.set_gen(tp.get_gen() + 1);
        tp.set_top_blk_id(id);
        cur_val = tp.to_integer();
    } while (!(m_top_blk_id.compare_exchange_weak(prev_val, cur_val)));
}

std::string FixedBlkAllocator::to_string() const {
    ostringstream oss;
    oss << "Total alloc blks = " << m_alloc_blk_cnt.load() << " ";
    oss << "m_top_blk_id=" << m_top_blk_id << "\n";
    return oss.str();
}
} // namespace homestore
