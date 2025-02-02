/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
/*
 * Homestore testing binaries shared common definitions, apis and data structures
 *
 */

#pragma once
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <iomgr/iomgr_config.hpp>
#include <homestore/homestore.hpp>

const std::string SPDK_ENV_VAR_STRING{"USER_WANT_SPDK"};
const std::string HTTP_SVC_ENV_VAR_STRING{"USER_WANT_HTTP_OFF"};
const std::string CP_WATCHDOG_TIMER_SEC{"USER_SET_CP_WD_TMR_SEC"};          // used in nightly test;
const std::string FLIP_SLOW_PATH_EVERY_NTH{"USER_SET_SLOW_PATH_EVERY_NTH"}; // used in nightly test;
const std::string BLKSTORE_FORMAT_OFF{"USER_WANT_BLKSTORE_FORMAT_OFF"};     // used for debug purpose;
const std::string USER_WANT_DIRECT_IO{"USER_WANT_DIRECT_IO"};               // used for HDD direct io mode;

SISL_OPTION_GROUP(test_common_setup,
                  (num_threads, "", "num_threads", "number of threads",
                   ::cxxopts::value< uint32_t >()->default_value("2"), "number"),
                  (num_devs, "", "num_devs", "number of devices to create",
                   ::cxxopts::value< uint32_t >()->default_value("2"), "number"),
                  (dev_size_mb, "", "dev_size_mb", "size of each device in MB",
                   ::cxxopts::value< uint64_t >()->default_value("1024"), "number"),
                  (device_list, "", "device_list", "Device List instead of default created",
                   ::cxxopts::value< std::vector< std::string > >(), "path [...]"),
                  (spdk, "", "spdk", "spdk", ::cxxopts::value< bool >()->default_value("false"), "true or false"));

using namespace homestore;

namespace test_common {

// Fix a port for http server
inline static void set_fixed_http_port(uint32_t http_port){
    IM_SETTINGS_FACTORY().modifiable_settings([http_port](auto& s) { s.io_env->http_port = http_port; });
    IM_SETTINGS_FACTORY().save();
    LOGINFO("http port = {}", http_port);
}

// generate random port for http server
inline static void set_random_http_port() {
    static std::random_device dev;
    static std::mt19937 rng(dev());
    std::uniform_int_distribution< std::mt19937::result_type > dist(1001u, 99999u);
    const uint32_t http_port = dist(rng);
    LOGINFO("random port generated = {}", http_port);
    IM_SETTINGS_FACTORY().modifiable_settings([http_port](auto& s) { s.io_env->http_port = http_port; });
    IM_SETTINGS_FACTORY().save();
}

class HSTestHelper {
private:
    static void remove_files(const std::vector< std::string >& file_paths) {
        for (const auto& fpath : file_paths) {
            if (std::filesystem::exists(fpath)) { std::filesystem::remove(fpath); }
        }
    }

    static void init_files(const std::vector< std::string >& file_paths, uint64_t dev_size) {
        remove_files(file_paths);
        for (const auto& fpath : file_paths) {
            std::ofstream ofs{fpath, std::ios::binary | std::ios::out | std::ios::trunc};
            std::filesystem::resize_file(fpath, dev_size);
        }
    }

    static std::vector< std::string > s_dev_names;

public:
    static void start_homestore(const std::string& test_name, float meta_pct, float data_log_pct, float ctrl_log_pct,
                                float index_pct, hs_init_starting_cb_t cb, bool restart = false) {
        auto const ndevices = SISL_OPTIONS["num_devs"].as< uint32_t >();
        auto const dev_size = SISL_OPTIONS["dev_size_mb"].as< uint64_t >() * 1024 * 1024;
        auto nthreads = SISL_OPTIONS["num_threads"].as< uint32_t >();
        auto is_spdk = SISL_OPTIONS["spdk"].as< bool >();

        if (restart) {
            shutdown_homestore(false);
            std::this_thread::sleep_for(std::chrono::seconds{5});
        }

        std::vector< homestore::dev_info > device_info;
        if (SISL_OPTIONS.count("device_list")) {
            s_dev_names = SISL_OPTIONS["device_list"].as< std::vector< std::string > >();
            LOGINFO("Taking input dev_list: {}",
                    std::accumulate(
                        s_dev_names.begin(), s_dev_names.end(), std::string(""),
                        [](const std::string& ss, const std::string& s) { return ss.empty() ? s : ss + "," + s; }));

            for (const auto& name : s_dev_names) {
                device_info.emplace_back(name, homestore::HSDevType::Data);
            }
        } else {
            /* create files */
            LOGINFO("creating {} device files with each of size {} ", ndevices, homestore::in_bytes(dev_size));
            for (uint32_t i{0}; i < ndevices; ++i) {
                s_dev_names.emplace_back(std::string{"/tmp/" + test_name + "_" + std::to_string(i + 1)});
            }

            if (!restart) { init_files(s_dev_names, dev_size); }
            for (const auto& fname : s_dev_names) {
                device_info.emplace_back(std::filesystem::canonical(fname).string(), homestore::HSDevType::Data);
            }
        }

        if (is_spdk) {
            LOGINFO("Spdk with more than 2 threads will cause overburden test systems, changing nthreads to 2");
            nthreads = 2;
        }

        LOGINFO("Starting iomgr with {} threads, spdk: {}", nthreads, is_spdk);
        ioenvironment.with_iomgr(nthreads, is_spdk);

        const uint64_t app_mem_size = ((ndevices * dev_size) * 15) / 100;
        LOGINFO("Initialize and start HomeStore with app_mem_size = {}", homestore::in_bytes(app_mem_size));

        homestore::hs_input_params params;
        params.app_mem_size = app_mem_size;
        params.data_devices = device_info;
        homestore::HomeStore::instance()
            ->with_params(params)
            .with_meta_service(meta_pct)
            .with_log_service(data_log_pct, ctrl_log_pct)
            .before_init_devices(std::move(cb))
            .init(true /* wait_for_init */);
    }

    static void shutdown_homestore(bool cleanup = true) {
        homestore::HomeStore::instance()->shutdown();
        homestore::HomeStore::reset_instance();
        iomanager.stop();

        if (cleanup) { remove_files(s_dev_names); }
    }
};
} // namespace test_common

// TODO: start_homestore should be moved here and called by each testing binaries
