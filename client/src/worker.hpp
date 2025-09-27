#pragma once

#include <cstdint>
#include <string>
#include "ipc.hpp"

class worker {
public:
    worker();
    ~worker();

    int run(const std::string& socket_path);

private:
    ipc m_ipc;

    bool check_root_privileges();
    void handle_workload_request(const std::string& request);

    // Workload functions
    void setup_vm(uint64_t workload_id, const nlohmann::json& params);
    void check_installed_apps(uint64_t workload_id, const nlohmann::json& params);
    void scan_wim_versions(uint64_t workload_id, const nlohmann::json& params);
    void install_vm(uint64_t workload_id, const nlohmann::json& params);
    void get_vm_status(uint64_t workload_id, const nlohmann::json& params);
    void start_vm(uint64_t workload_id, const nlohmann::json& params);
    void stop_vm(uint64_t workload_id, const nlohmann::json& params);
    void remove_vm(uint64_t workload_id, const nlohmann::json& params);
};
