//
// Created by Kadayam, Hari on 06/11/17.
//

#include <iostream>
#include <sds_logging/logging.h>
#include "device/device.h"
#include <fcntl.h>
#include "blkstore.hpp"
#include "device/virtual_dev.hpp"

using namespace std;
using namespace homestore;

SDS_INIT_LOGGING

homestore::DeviceManager* dev_mgr = nullptr;
homestore::BlkStore< homestore::VdevFixedBlkAllocatorPolicy >* blk_store;
homestore::Cache< BlkId >* glob_cache = nullptr;

#define MAX_CACHE_SIZE 2 * 1024 * 1024 * 1024

AbstractVirtualDev* new_vdev_found(homestore::vdev_info_block* vb) {
    LOGINFO("New virtual device found id = {} size = {}", vb->vdev_id, vb->size);
    blk_store = new homestore::BlkStore< homestore::VdevFixedBlkAllocatorPolicy >(dev_mgr, glob_cache, vb,
                                                                                  WRITETHRU_CACHE, 8192);
    return blk_store->get_vdev();
}

int main(int argc, char** argv) {
    std::vector< std::string > dev_names;
    bool create = ((argc > 1) && (!strcmp(argv[1], "-c")));

    for (auto i : boost::irange(create ? 2 : 1, argc)) {
        dev_names.emplace_back(argv[i]);
    }

    /* Create the cache entry */
    glob_cache = new homestore::Cache< BlkId >(MAX_CACHE_SIZE, 8192);
    assert(glob_cache);

    /* Create/Load the devices */
    dev_mgr = new homestore::DeviceManager(new_vdev_found, 0);
    try {
        dev_mgr->add_devices(dev_names);
    } catch (std::exception& e) {
        LOGCRITICAL("Exception info {}", e.what());
        exit(1);
    }
    auto devs = dev_mgr->get_all_devices();

    /* Create a blkstore */
    if (create) {
        LOGINFO("Creating BlkStore");
        uint64_t size = 512 * 1024 * 1024;
        blk_store = new homestore::BlkStore< homestore::VdevFixedBlkAllocatorPolicy >(dev_mgr, glob_cache, size,
                                                                                      WRITETHRU_CACHE, 1, 8192);
    }

    homestore::BlkId bids[100];
    homestore::blk_alloc_hints hints;
    hints.desired_temp = 0;
    hints.dev_id_hint = -1;

    boost::intrusive_ptr< BlkBuffer > bbufs[100];
    for (auto i = 0; i < 100; i++) {
        uint8_t nblks = 1;
        // blk_store->alloc_blk(nblks * BLKSTORE_BLK_SIZE, hints, &bids[i]);

        bbufs[i] = blk_store->alloc_blk_cached(nblks * BLKSTORE_BLK_SIZE, hints, &bids[i]);
        LOGINFO("Requested nblks: {} Allocation info: {}", (uint32_t)nblks, bids[i].to_string());
        memset(bbufs[i]->at_offset(0).bytes, i, bbufs[i]->at_offset(0).size);
    }

    // char bufs[100][8192];
    for (auto i = 0; i < 100; i++) {
        // memset(bufs[i], i, 8192);
        // sisl::blob b = {(uint8_t *)&bufs[i], 8192};

        // boost::intrusive_ptr< BlkBuffer > bbuf = blk_store->write(bids[i], b);
        blk_store->write(bids[i], bbufs[i]);
        LOGINFO("Written on {} for 8192 bytes", bids[i].to_string());
    }

    for (auto i = 0; i < 100; i++) {
        LOGINFO("Read from {} for 8192 bytes", bids[i].to_string());

        boost::intrusive_ptr< BlkBuffer > bbuf = blk_store->read(bids[i], 0, 8192);
        omds::blob b = bbuf->at_offset(0);
        assert(b.size == 8192);
        for (auto j = 0; j < b.size; j++) {
            assert(b.bytes[j] == i);
        }
    }
}
