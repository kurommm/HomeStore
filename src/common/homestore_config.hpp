﻿
#ifndef _HOMESTORE_CONFIG_HPP_
#define _HOMESTORE_CONFIG_HPP_

#include "homestore_header.hpp"
#include <sds_options/options.h>
#include <engine/common/error.h>
#include <cassert>
#include <array>
#include <cstdint>
#include <boost/intrusive_ptr.hpp>
#include <settings/settings.hpp>
#include "engine/common/generated/homestore_config_generated.h"
#include <nlohmann/json.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/optional.hpp>
#include <iomgr/iomgr.hpp>

SETTINGS_INIT(homestorecfg::HomeStoreSettings, homestore_config,
              SDS_OPTIONS.count("config_path") ? SDS_OPTIONS["config_path"].as< std::string >() : "");

/* DM info size depends on these three parameters. If below parameter changes then we have to add
 * the code for upgrade/revert.
 */
constexpr uint32_t MAX_CHUNKS = 128;
constexpr uint32_t MAX_VDEVS = 16;
constexpr uint32_t MAX_PDEVS = 8;

namespace homestore {
#define HS_DYNAMIC_CONFIG_WITH(...) SETTINGS(homestore_config, __VA_ARGS__)
#define HS_DYNAMIC_CONFIG_THIS(...) SETTINGS_THIS(homestore_config, __VA_ARGS__)
#define HS_DYNAMIC_CONFIG_WITH_CAP(...) SETTINGS_THIS_CAP1(homestore_config, __VA_ARGS__)
#define HS_DYNAMIC_CONFIG(...) SETTINGS_VALUE(homestore_config, __VA_ARGS__)

#define HS_SETTINGS_FACTORY() SETTINGS_FACTORY(homestore_config)

#define HS_STATIC_CONFIG(cfg) homestore::HomeStoreStaticConfig::instance().cfg

/* This is the optional parameteres which should be given by its consumers only when there is no
 * system command to get these parameteres directly from disks. Or Consumer want to override
 * the default values.
 */

struct cap_attrs {
    uint64_t used_data_size = 0;
    uint64_t used_index_size = 0;
    uint64_t used_total_size = 0;
    uint64_t initial_total_size = 0;
    std::string to_string() {
        std::stringstream ss;
        ss << "used_data_size = " << used_data_size << ", used_index_size = " << used_index_size
           << ", used_total_size = " << used_total_size << ", initial_total_size = " << initial_total_size;
        return ss.str();
    }
    void add(const cap_attrs& other) {
        used_data_size += other.used_data_size;
        used_index_size += other.used_index_size;
        used_total_size += other.used_total_size;
        initial_total_size += other.initial_total_size;
    }
};

struct hs_input_params {
public:
    std::vector< dev_info > devices;                                       // name of the devices.
    iomgr::iomgr_drive_type device_type{iomgr::iomgr_drive_type::unknown}; // Type of the device
    bool is_file{false};                                                   // Is the devices a file or raw device
    boost::uuids::uuid system_uuid;                                        // Deprecated. UUID assigned to the system
    io_flag open_flags = io_flag::DIRECT_IO;

    uint32_t min_virtual_page_size = 4096;          // minimum page size supported. Ideally it should be 4k.
    uint64_t app_mem_size = 1 * 1024 * 1024 * 1024; // memory available for the app (including cache)
    bool disk_init = false;                         // Deprecated. true if disk has to be initialized.
    bool is_read_only = false;                      // Is read only
    bool is_restricted_mode = false;                // boot in restricted mode
    bool start_http = true;

#ifdef _PRERELEASE
    bool force_reinit = false;
#endif

    /* optional parameters - if provided will override the startup config */
    boost::optional< iomgr::drive_attributes > drive_attr;

    nlohmann::json to_json() const {
        nlohmann::json json;
        json["system_uuid"] = boost::uuids::to_string(system_uuid);
        json["devices"] = nlohmann::json::array();
        for (auto& d : devices) {
            json["devices"].push_back(d.dev_names);
        }
        json["open_flags"] = open_flags;
        json["device_type"] = enum_name(device_type);
        json["is_read_only"] = is_read_only;

        json["min_virtual_page_size"] = min_virtual_page_size;
        json["app_mem_size"] = app_mem_size;

        return json;
    }
};

struct hs_engine_config {
    size_t min_io_size = 8192; // minimum io size supported by

    uint64_t max_chunks = MAX_CHUNKS; // These 3 parameters can be ONLY changed with upgrade/revert from device manager
    uint64_t max_vdevs = MAX_VDEVS;
    uint64_t max_pdevs = MAX_PDEVS;

    nlohmann::json to_json() const {
        nlohmann::json json;
        json["min_io_size"] = min_io_size;
        json["max_chunks"] = max_chunks;
        json["max_vdevs"] = max_vdevs;
        json["max_pdevs"] = max_pdevs;
        return json;
    }
};

struct HomeStoreStaticConfig {
    static HomeStoreStaticConfig& instance() {
        static HomeStoreStaticConfig s_inst;
        return s_inst;
    }

    iomgr::drive_attributes drive_attr;
    hs_engine_config engine;
    hs_input_params input;

    nlohmann::json to_json() const {
        nlohmann::json json;
        json["DriveAttributes"] = drive_attr.to_json();
        json["GenericConfig"] = engine.to_json();
        json["InputParameters"] = input.to_json();
        return json;
    }

#ifndef NDEBUG
    void validate() {
        assert(drive_attr.phys_page_size >= drive_attr.atomic_phys_page_size);
        assert(drive_attr.phys_page_size >= engine.min_io_size);
    }
#endif
};

class HomeStoreDynamicConfig {
public:
    static constexpr std::array< double, 9 > default_slab_distribution() {
        // Aassuming blk_size=4K      [4K,   8K,  16K, 32K, 64K,  128K, 256K, 512K, 1M ]
        return std::array< double, 9 >{15.0, 7.0, 7.0, 6.0, 10.0, 10.0, 10.0, 10.0, 25.0};

        // return std::array< double, 9 >{20.0, 10.0, 10.0, 10.0, 36.0, 4.0, 4.0, 4.0, 2.0};
    }

    // This method sets up the default for settings factory when there is no override specified in the json
    // file and .fbs cannot specify default because they are not scalar.
    static void init_settings_default() {
        bool is_modified = false;

        HS_SETTINGS_FACTORY().modifiable_settings([&is_modified](auto& s) {
            /* Setup slab config of blk alloc cache, if they are not set already - first time */
            auto& slab_pct_dist = s.blkallocator.free_blk_slab_distribution;
            if (slab_pct_dist.size() == 0) {
                LOGINFO("Free Blks Slab distribution is not initialized, possibly first boot - setting with defaults");

                // Slab distribution is not initialized, defaults
                const auto d = default_slab_distribution();
                slab_pct_dist.insert(slab_pct_dist.begin(), std::cbegin(d), std::cend(d));
                is_modified = true;
            }

            // Any more default overrides or set non-scalar entries come here
        });

        if (is_modified) {
            LOGINFO("Some settings are defaultted or overridden explicitly in the code, saving the new settings");
            HS_SETTINGS_FACTORY().save();
        }
    }
};
constexpr uint32_t BLK_NUM_BITS = 32;
constexpr uint32_t NBLKS_BITS = 8;
constexpr uint32_t CHUNK_NUM_BITS = 8;
constexpr uint32_t BLKID_SIZE_BITS = BLK_NUM_BITS + NBLKS_BITS + CHUNK_NUM_BITS;
constexpr uint32_t MEMPIECE_ENCODE_MAX_BITS = 8;
constexpr uint64_t MAX_NBLKS = ((1 << NBLKS_BITS) - 1);
constexpr uint64_t MAX_CHUNK_ID = ((1 << CHUNK_NUM_BITS) - 2); // one less to indicate invalid chunks
constexpr uint64_t BLKID_SIZE = (BLKID_SIZE_BITS / 8) + (((BLKID_SIZE_BITS % 8) != 0) ? 1 : 0);
constexpr uint32_t BLKS_PER_PORTION = 1024;
constexpr uint32_t TOTAL_SEGMENTS = 8;
constexpr uint32_t MAX_BLK_NUM_BITS_PER_CHUNK = ((1lu << BLK_NUM_BITS) - 1);

/* NOTE: it can give size more then the size passed in argument to make it aligned */
// #define ALIGN_SIZE(size, align) (((size % align) == 0) ? size : (size + (align - (size % align))))

/* NOTE: it can give size less then size passed in argument to make it aligned */
// #define ALIGN_SIZE_TO_LEFT(size, align) (((size % align) == 0) ? size : (size - (size % align)))

#define MEMVEC_MAX_IO_SIZE (HS_STATIC_CONFIG(engine.min_io_size) * ((1 << MEMPIECE_ENCODE_MAX_BITS) - 1))
#define MIN_CHUNK_SIZE (HS_STATIC_CONFIG(drive_attr.phys_page_size) * BLKS_PER_PORTION * TOTAL_SEGMENTS)
#define MAX_CHUNK_SIZE                                                                                                 \
    sisl::round_down((MAX_BLK_NUM_BITS_PER_CHUNK * HS_STATIC_CONFIG(engine.min_io_size)), MIN_CHUNK_SIZE) // 16T

/* TODO: we store global unique ID in blkid. Instead it we only store chunk offset then
 * max cacapity will increase from MAX_CHUNK_SIZE to MAX_CHUNKS * MAX_CHUNK_SIZE.
 */
#define MAX_SUPPORTED_CAP (MAX_CHUNKS * MAX_CHUNK_SIZE)

#define MAX_UUID_LEN 128

/* 1 % of disk space is reserved for volume sb chunks. With 8k page it
 * will come out to be around 7 GB.
 */
#define MIN_DISK_CAP_SUPPORTED (MIN_CHUNK_SIZE * 100 / 99 + MIN_CHUNK_SIZE)
} // namespace homestore
#endif
