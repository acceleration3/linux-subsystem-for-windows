#pragma once

#include <string>
#include <vector>
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <nlohmann/json.hpp>

struct vm_config {
    std::string name;
    std::string iso_path;
    std::string windows_edition;
    std::string admin_username;
    std::string admin_password;
    int memory_gb;
    int cpu_cores;
    int disk_gb;
    bool hardware_acceleration = true;
    bool use_autounattend = true;
    std::string autounattend_iso_path; // Path to separate autounattend ISO
    std::string virtio_iso_path;       // Path to VirtIO guest tools ISO
};

class vm_manager {
public:
    vm_manager();
    ~vm_manager();

    vm_manager(const vm_manager&) = delete;
    vm_manager& operator=(const vm_manager&) = delete;

    vm_manager(vm_manager&&) = delete;
    vm_manager& operator=(vm_manager&&) = delete;

    // Connection management
    bool connect();
    void disconnect();
    bool is_connected() const;

    // VM operations
    bool create_vm(const vm_config& config);
    bool start_vm(const std::string& vm_name);
    bool stop_vm(const std::string& vm_name);
    bool delete_vm(const std::string& vm_name);
    bool vm_exists(const std::string& vm_name);

    // VM information
    nlohmann::json get_vm_info(const std::string& vm_name);
    std::vector<std::string> list_vms();

    // Network management
    bool is_network_active(const std::string& network_name = "default");
    bool start_network(const std::string& network_name = "default");
    bool ensure_network_available(const std::string& network_name = "default");
    std::string get_network_diagnostics(const std::string& network_name = "default");

    // Autounattend functionality (using system calls)
    std::string create_modified_iso_with_autounattend(const vm_config& config);

    // Error handling
    std::string get_last_error() const;

private:
    virConnectPtr m_connection;
    std::string m_last_error;

    // Helper methods
    void set_error(const std::string& error);
    std::string generate_vm_xml(const vm_config& config);
    std::string create_disk_image(const vm_config& config);
    bool validate_config(const vm_config& config);
};
