#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <wimlib.h>
#include "templates/libvirt_domain_template.hpp"
#include "vm_manager.hpp"

namespace {
std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    return ss.str();
}

std::string generate_mac_address() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    std::stringstream ss;
    ss << "52:54:00:";
    ss << std::hex << std::setfill('0') << std::setw(2) << dis(gen) << ":";
    ss << std::hex << std::setfill('0') << std::setw(2) << dis(gen) << ":";
    ss << std::hex << std::setfill('0') << std::setw(2) << dis(gen);
    return ss.str();
}

std::string replace_string(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}
} // namespace

vm_manager::vm_manager() : m_connection(nullptr) {}

vm_manager::~vm_manager() {
    disconnect();
}

bool vm_manager::connect() {
    if (m_connection) {
        return true; // Already connected
    }

    m_connection = virConnectOpen("qemu:///system");
    if (!m_connection) {
        set_error("Failed to connect to libvirt daemon. Make sure libvirtd is running and you have "
                  "proper permissions.");
        return false;
    }

    std::cout << "[VM Manager] Connected to libvirt daemon" << std::endl;
    return true;
}

void vm_manager::disconnect() {
    if (m_connection) {
        virConnectClose(m_connection);
        m_connection = nullptr;
        std::cout << "[VM Manager] Disconnected from libvirt daemon" << std::endl;
    }
}

bool vm_manager::is_connected() const {
    return m_connection != nullptr;
}

bool vm_manager::create_vm(const vm_config& config) {
    if (!is_connected()) {
        set_error("Not connected to libvirt daemon");
        return false;
    }

    if (!validate_config(config)) {
        return false;
    }

    // Ensure network is available
    if (!ensure_network_available("default")) {
        set_error("Failed to ensure network availability: " + get_last_error());
        return false;
    }

    // Check if VM already exists
    if (vm_exists(config.name)) {
        set_error("VM '" + config.name + "' already exists");
        return false;
    }

    // Create disk image
    std::string disk_path = create_disk_image(config);
    if (disk_path.empty()) {
        return false;
    }

    // Generate VM XML configuration
    std::string xml_config = generate_vm_xml(config);
    if (xml_config.empty()) {
        return false;
    }

    // Create the VM
    virDomainPtr domain = virDomainDefineXML(m_connection, xml_config.c_str());
    if (!domain) {
        set_error("Failed to define VM: " + std::string(virGetLastErrorMessage()));
        return false;
    }

    std::cout << "[VM Manager] VM '" << config.name << "' created successfully" << std::endl;
    virDomainFree(domain);
    return true;
}

bool vm_manager::start_vm(const std::string& vm_name) {
    if (!is_connected()) {
        set_error("Not connected to libvirt daemon");
        return false;
    }

    virDomainPtr domain = virDomainLookupByName(m_connection, vm_name.c_str());
    if (!domain) {
        set_error("VM '" + vm_name + "' not found");
        return false;
    }

    int result = virDomainCreate(domain);
    virDomainFree(domain);

    if (result < 0) {
        set_error("Failed to start VM '" + vm_name + "': " + std::string(virGetLastErrorMessage()));
        return false;
    }

    std::cout << "[VM Manager] VM '" << vm_name << "' started successfully" << std::endl;
    return true;
}

bool vm_manager::stop_vm(const std::string& vm_name) {
    if (!is_connected()) {
        set_error("Not connected to libvirt daemon");
        return false;
    }

    virDomainPtr domain = virDomainLookupByName(m_connection, vm_name.c_str());
    if (!domain) {
        set_error("VM '" + vm_name + "' not found");
        return false;
    }

    int result = virDomainShutdown(domain);
    virDomainFree(domain);

    if (result < 0) {
        set_error("Failed to stop VM '" + vm_name + "': " + std::string(virGetLastErrorMessage()));
        return false;
    }

    std::cout << "[VM Manager] VM '" << vm_name << "' stopped successfully" << std::endl;
    return true;
}

bool vm_manager::delete_vm(const std::string& vm_name) {
    if (!is_connected()) {
        set_error("Not connected to libvirt daemon");
        return false;
    }

    virDomainPtr domain = virDomainLookupByName(m_connection, vm_name.c_str());
    if (!domain) {
        set_error("VM '" + vm_name + "' not found");
        return false;
    }

    // First, destroy the domain if it's running
    int state;
    int reason;
    if (virDomainGetState(domain, &state, &reason, 0) == 0) {
        if (state == VIR_DOMAIN_RUNNING) {
            virDomainDestroy(domain);
        }
    }

    // Then undefine the domain
    int result = virDomainUndefine(domain);
    virDomainFree(domain);

    if (result < 0) {
        set_error("Failed to delete VM '" + vm_name +
                  "': " + std::string(virGetLastErrorMessage()));
        return false;
    }

    // Clean up disk image
    std::string disk_path = "/var/lib/libvirt/images/" + vm_name + ".qcow2";
    if (std::filesystem::exists(disk_path)) {
        std::filesystem::remove(disk_path);
    }

    std::cout << "[VM Manager] VM '" << vm_name << "' deleted successfully" << std::endl;
    return true;
}

bool vm_manager::vm_exists(const std::string& vm_name) {
    if (!is_connected()) {
        return false;
    }

    virDomainPtr domain = virDomainLookupByName(m_connection, vm_name.c_str());
    if (domain) {
        virDomainFree(domain);
        return true;
    }
    return false;
}

nlohmann::json vm_manager::get_vm_info(const std::string& vm_name) {
    nlohmann::json info;

    if (!is_connected()) {
        info["error"] = "Not connected to libvirt daemon";
        return info;
    }

    virDomainPtr domain = virDomainLookupByName(m_connection, vm_name.c_str());
    if (!domain) {
        info["error"] = "VM '" + vm_name + "' not found";
        return info;
    }

    // Get basic VM information
    info["name"] = vm_name;

    // Get VM state
    int state;
    int reason;
    if (virDomainGetState(domain, &state, &reason, 0) == 0) {
        switch (state) {
        case VIR_DOMAIN_RUNNING:
            info["state"] = "running";
            break;
        case VIR_DOMAIN_BLOCKED:
            info["state"] = "blocked";
            break;
        case VIR_DOMAIN_PAUSED:
            info["state"] = "paused";
            break;
        case VIR_DOMAIN_SHUTDOWN:
            info["state"] = "shutdown";
            break;
        case VIR_DOMAIN_SHUTOFF:
            info["state"] = "shutoff";
            break;
        case VIR_DOMAIN_CRASHED:
            info["state"] = "crashed";
            break;
        case VIR_DOMAIN_PMSUSPENDED:
            info["state"] = "suspended";
            break;
        default:
            info["state"] = "unknown";
            break;
        }
    }

    // Get memory and CPU info
    unsigned long max_mem = virDomainGetMaxMemory(domain);
    if (max_mem > 0) {
        info["memory_mb"] = max_mem / 1024;
    }

    int cpu_count = virDomainGetVcpusFlags(domain, VIR_DOMAIN_VCPU_MAXIMUM);
    if (cpu_count > 0) {
        info["cpu_count"] = cpu_count;
    }

    virDomainFree(domain);
    return info;
}

std::vector<std::string> vm_manager::list_vms() {
    std::vector<std::string> vms;

    if (!is_connected()) {
        return vms;
    }

    int num_domains = virConnectNumOfDefinedDomains(m_connection);
    if (num_domains < 0) {
        set_error("Failed to get number of domains");
        return vms;
    }

    if (num_domains > 0) {
        char** names = new char*[num_domains];
        int result = virConnectListDefinedDomains(m_connection, names, num_domains);

        if (result >= 0) {
            for (int i = 0; i < result; ++i) {
                vms.push_back(std::string(names[i]));
                free(names[i]);
            }
        }
        delete[] names;
    }

    return vms;
}

std::string vm_manager::get_last_error() const {
    return m_last_error;
}

void vm_manager::set_error(const std::string& error) {
    m_last_error = error;
    std::cerr << "[VM Manager] Error: " << error << std::endl;
}

std::string vm_manager::generate_vm_xml(const vm_config& config) {
    // Convert memory from GB to KiB
    unsigned long memory_kib = config.memory_gb * 1024 * 1024;

    // Generate UUID and MAC address
    std::string uuid = generate_uuid();
    std::string mac_address = generate_mac_address();

    // Start with the template
    std::string xml = templates::LIBVIRT_DOMAIN_TEMPLATE;

    // Replace template variables
    xml = replace_string(xml, "{{VM_NAME}}", config.name);
    xml = replace_string(xml, "{{VM_UUID}}", uuid);
    xml = replace_string(xml, "{{MEMORY_KB}}", std::to_string(memory_kib));
    xml = replace_string(xml, "{{CPU_COUNT}}", std::to_string(config.cpu_cores));
    xml = replace_string(xml, "{{DISK_PATH}}", "/var/lib/libvirt/images/" + config.name + ".qcow2");
    xml = replace_string(xml, "{{WINDOWS_ISO_PATH}}", config.iso_path);
    xml = replace_string(xml, "{{AUTOUNATTEND_ISO_PATH}}", config.autounattend_iso_path);
    xml = replace_string(xml, "{{VIRTIO_ISO_PATH}}", config.virtio_iso_path);
    xml = replace_string(xml, "{{MAC_ADDRESS}}", mac_address);
    xml = replace_string(xml, "{{NETWORK_NAME}}", "default");
    xml = replace_string(xml, "{{RENDER_NODE}}", "/dev/dri/by-path/pci-0000:03:00.0-render");

    return xml;
}

std::string vm_manager::create_disk_image(const vm_config& config) {
    std::string disk_path = "/var/lib/libvirt/images/" + config.name + ".qcow2";

    // Create the disk image using qemu-img
    std::string cmd =
        "qemu-img create -f qcow2 " + disk_path + " " + std::to_string(config.disk_gb) + "G";

    std::cout << "[VM Manager] Creating disk image: " << cmd << std::endl;
    std::cout << "[VM Manager] Disk size: " << config.disk_gb << "GB" << std::endl;

    int result = system(cmd.c_str());
    if (result != 0) {
        set_error("Failed to create disk image (exit code: " + std::to_string(result) + ")");
        return "";
    }

    // Verify the disk was created and has correct size
    if (!std::filesystem::exists(disk_path)) {
        set_error("Disk image was not created: " + disk_path);
        return "";
    }

    // Check disk size
    auto file_size = std::filesystem::file_size(disk_path);
    std::cout << "[VM Manager] Created disk size: " << file_size << " bytes" << std::endl;

    // Set proper permissions
    std::string chown_cmd = "chown libvirt-qemu:libvirt-qemu " + disk_path;
    int chown_result = system(chown_cmd.c_str());
    if (chown_result != 0) {
        std::cout << "[VM Manager] Warning: Failed to set permissions (exit code: " << chown_result
                  << ")" << std::endl;
    }

    return disk_path;
}

bool vm_manager::validate_config(const vm_config& config) {
    if (config.name.empty()) {
        set_error("VM name cannot be empty");
        return false;
    }

    if (config.iso_path.empty()) {
        set_error("ISO path cannot be empty");
        return false;
    }

    if (!std::filesystem::exists(config.iso_path)) {
        set_error("ISO file does not exist: " + config.iso_path);
        return false;
    }

    if (config.memory_gb < 1 || config.memory_gb > 128) {
        set_error("Memory must be between 1 and 128 GB");
        return false;
    }

    if (config.cpu_cores < 1 || config.cpu_cores > 128) {
        set_error("CPU cores must be between 1 and 128");
        return false;
    }

    if (config.disk_gb < 30 || config.disk_gb > 1024) {
        set_error("Disk size must be between 30 and 1024 GB");
        return false;
    }

    return true;
}

bool vm_manager::is_network_active(const std::string& network_name) {
    if (!is_connected()) {
        set_error("Not connected to libvirt daemon");
        return false;
    }

    virNetworkPtr network = virNetworkLookupByName(m_connection, network_name.c_str());
    if (!network) {
        set_error("Network '" + network_name + "' not found");
        return false;
    }

    bool is_active = virNetworkIsActive(network) == 1;
    virNetworkFree(network);

    return is_active;
}

bool vm_manager::start_network(const std::string& network_name) {
    if (!is_connected()) {
        set_error("Not connected to libvirt daemon");
        return false;
    }

    virNetworkPtr network = virNetworkLookupByName(m_connection, network_name.c_str());
    if (!network) {
        set_error("Network '" + network_name + "' not found");
        return false;
    }

    int result = virNetworkCreate(network);
    virNetworkFree(network);

    if (result != 0) {
        std::string error_msg = virGetLastErrorMessage();

        // Check if the error is due to interface already in use
        if (error_msg.find("already in use") != std::string::npos ||
            error_msg.find("Network is already in use") != std::string::npos) {
            std::cout
                << "[VM Manager] Network interface conflict detected. Attempting to resolve..."
                << std::endl;

            // Try to destroy and recreate the network
            virNetworkPtr network_destroy =
                virNetworkLookupByName(m_connection, network_name.c_str());
            if (network_destroy) {
                virNetworkDestroy(network_destroy);
                virNetworkFree(network_destroy);

                // Wait a moment for cleanup
                std::this_thread::sleep_for(std::chrono::seconds(2));

                // Try to start again
                virNetworkPtr network_retry =
                    virNetworkLookupByName(m_connection, network_name.c_str());
                if (network_retry) {
                    result = virNetworkCreate(network_retry);
                    virNetworkFree(network_retry);
                }
            }
        }

        if (result != 0) {
            set_error("Failed to start network '" + network_name + "': " + error_msg +
                      ". To resolve this issue, try one of these solutions:\n"
                      "1. Restart libvirtd: sudo systemctl restart libvirtd\n"
                      "2. Remove conflicting interface: sudo ip link delete virbr0\n"
                      "3. Undefine and recreate network: virsh net-undefine default && virsh "
                      "net-define <network-xml>");
            return false;
        }
    }

    std::cout << "[VM Manager] Network '" << network_name << "' started successfully" << std::endl;
    return true;
}

bool vm_manager::ensure_network_available(const std::string& network_name) {
    if (is_network_active(network_name)) {
        std::cout << "[VM Manager] Network '" << network_name << "' is already active" << std::endl;
        return true;
    }

    std::cout << "[VM Manager] Starting network '" << network_name << "'..." << std::endl;
    return start_network(network_name);
}

std::string vm_manager::get_network_diagnostics(const std::string& network_name) {
    std::string diagnostics = "Network Diagnostics for '" + network_name + "':\n";

    if (!is_connected()) {
        diagnostics += "- ERROR: Not connected to libvirt daemon\n";
        return diagnostics;
    }

    // Check if network exists
    virNetworkPtr network = virNetworkLookupByName(m_connection, network_name.c_str());
    if (!network) {
        diagnostics += "- ERROR: Network '" + network_name + "' not found\n";
        diagnostics += "- SOLUTION: Define the network first\n";
        return diagnostics;
    }

    // Check network state
    bool is_active = virNetworkIsActive(network) == 1;
    diagnostics += "- Network state: " + std::string(is_active ? "ACTIVE" : "INACTIVE") + "\n";

    // Get network XML for more details
    char* xml_desc = virNetworkGetXMLDesc(network, 0);
    if (xml_desc) {
        std::string xml_str(xml_desc);
        free(xml_desc);

        // Extract bridge name
        size_t bridge_pos = xml_str.find("<bridge name='");
        if (bridge_pos != std::string::npos) {
            size_t start = bridge_pos + 14;
            size_t end = xml_str.find("'", start);
            if (end != std::string::npos) {
                std::string bridge_name = xml_str.substr(start, end - start);
                diagnostics += "- Bridge interface: " + bridge_name + "\n";

                // Check if bridge interface exists
                std::string check_cmd = "ip link show " + bridge_name + " >/dev/null 2>&1";
                int result = system(check_cmd.c_str());
                diagnostics +=
                    "- Bridge interface exists: " + std::string(result == 0 ? "YES" : "NO") + "\n";

                if (result == 0) {
                    // Check bridge state
                    std::string state_cmd =
                        "ip link show " + bridge_name + " | grep -o 'state [A-Z]*'";
                    FILE* pipe = popen(state_cmd.c_str(), "r");
                    if (pipe) {
                        char buffer[128];
                        if (fgets(buffer, sizeof(buffer), pipe)) {
                            diagnostics += "- Bridge state: " + std::string(buffer);
                        }
                        pclose(pipe);
                    }
                }
            }
        }
    }

    virNetworkFree(network);

    if (!is_active) {
        diagnostics += "\nSOLUTIONS TO START NETWORK:\n";
        diagnostics += "1. Try: virsh net-start " + network_name + "\n";
        diagnostics += "2. If interface conflict: sudo ip link delete virbr0\n";
        diagnostics += "3. If still failing: sudo systemctl restart libvirtd\n";
        diagnostics += "4. Last resort: virsh net-undefine " + network_name + " && redefine\n";
    }

    return diagnostics;
}