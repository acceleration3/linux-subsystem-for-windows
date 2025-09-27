#include <cstdlib>
#include "application.hpp"
#include "autounattend_manager.hpp"
#include "util/defer.hpp"
#include "vm_manager.hpp"
#include "worker.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <wimlib.h>

// Helper structure for WIM image information
struct wim_image_info {
    std::string name;
    std::string display_name;
    std::string description;
    std::string display_description;
};

worker::worker() = default;

worker::~worker() = default;

int worker::run(const std::string& socket_path) {
    std::cout << "[Worker] Running" << std::endl;

    if (!check_root_privileges()) {
        return 1;
    }

    if (!m_ipc.connect_to_server(socket_path)) {
        return 1;
    }

    std::cout << "[Worker] Connected to client socket" << std::endl;

    // Main worker loop - listen for workload requests
    while (m_ipc.is_connected()) {
        try {
            auto request = m_ipc.receive_workload_request();
            uint64_t workload_id = std::get<0>(request);
            workload_type workload = std::get<1>(request);
            nlohmann::json params = std::get<2>(request);

            // Launch each workload in its own thread
            std::thread workload_thread([this, workload_id, workload, params]() {
                try {
                    switch (workload) {
                    case workload_type::check_installed_apps:
                        std::cout << "[Worker] Received check_installed_apps request (ID: "
                                  << workload_id << ")" << std::endl;
                        check_installed_apps(workload_id, params);
                        break;
                    case workload_type::scan_wim_versions:
                        std::cout << "[Worker] Received scan_wim_versions request (ID: "
                                  << workload_id << ")" << std::endl;
                        scan_wim_versions(workload_id, params);
                        break;
                    case workload_type::install_vm:
                        std::cout << "[Worker] Received install_vm request (ID: " << workload_id
                                  << ")" << std::endl;
                        install_vm(workload_id, params);
                        break;
                    case workload_type::get_vm_status:
                        std::cout << "[Worker] Received get_vm_status request (ID: " << workload_id
                                  << ")" << std::endl;
                        get_vm_status(workload_id, params);
                        break;
                    case workload_type::start_vm:
                        std::cout << "[Worker] Received start_vm request (ID: " << workload_id
                                  << ")" << std::endl;
                        start_vm(workload_id, params);
                        break;
                    case workload_type::stop_vm:
                        std::cout << "[Worker] Received stop_vm request (ID: " << workload_id << ")"
                                  << std::endl;
                        stop_vm(workload_id, params);
                        break;
                    case workload_type::remove_vm:
                        std::cout << "[Worker] Received remove_vm request (ID: " << workload_id
                                  << ")" << std::endl;
                        remove_vm(workload_id, params);
                        break;
                    default:
                        std::cout << "[Worker] Received invalid workload request" << std::endl;
                        break;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[Worker] Exception in workload thread (ID: " << workload_id
                              << "): " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[Worker] Unknown exception in workload thread (ID: "
                              << workload_id << ")" << std::endl;
                }
            });

            // Detach the thread so it runs independently
            workload_thread.detach();
        } catch (const std::exception& e) {
            std::cerr << "[Worker] Exception in main loop: " << e.what() << std::endl;
            break;
        } catch (...) {
            std::cerr << "[Worker] Unknown exception in main loop" << std::endl;
            break;
        }
    }

    return 0;
}

bool worker::check_root_privileges() {
    int uid = getuid();
    std::cout << "[Worker] UID: " << uid << std::endl;

    if (uid != 0) {
        std::cerr << "[Worker] Requires root privileges" << std::endl;
        return false;
    }

    std::cout << "[Worker] Running in root mode" << std::endl;
    return true;
}

void worker::check_installed_apps(uint64_t workload_id, const nlohmann::json& params) {
    std::cout << "[Worker] Checking installed applications (ID: " << workload_id << ")..."
              << std::endl;

    // Send in-progress status
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Scanning for installed applications...");

    // TODO: Implement app checking logic
    // This would involve scanning system for installed applications

    // Simulate some work
    sleep(1);

    std::cout << "[Worker] Application check completed (ID: " << workload_id << ")" << std::endl;

    // Send completion status with structured data
    std::string result = R"({
        "installed_apps": [
            {"name": "Firefox", "version": "120.0", "path": "/usr/bin/firefox"},
            {"name": "VSCode", "version": "1.85.0", "path": "/usr/bin/code"},
            {"name": "GIMP", "version": "2.10.34", "path": "/usr/bin/gimp"}
        ],
        "total_count": 3
    })";

    m_ipc.send_workload_response(workload_id, workload_status::completed, result);
}

void worker::scan_wim_versions(uint64_t workload_id, const nlohmann::json& params) {
    std::cout << "[Worker] Scanning WIM versions (ID: " << workload_id << ")..." << std::endl;
    std::cout << "[Worker] Parameters: " << params.dump() << std::endl;

    // Extract parameters
    std::string iso_path = params.value("iso_path", "");
    if (iso_path.empty()) {
        m_ipc.send_workload_response(workload_id, workload_status::error, "No ISO path provided");
        return;
    }

    // Send in-progress status
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Scanning WIM images in ISO...");

    // Inline WIM scanning using wimlib
    std::vector<wim_image_info> images;

    // Create a temporary mount point
    std::string mount_point = "/tmp/lsw_mount_" + std::to_string(getpid());
    std::string wim_path = mount_point + "/sources/install.wim";

    // Mount the ISO
    std::string mount_cmd =
        "mkdir -p " + mount_point + " && mount -o loop,ro " + iso_path + " " + mount_point;
    int mount_result = system(mount_cmd.c_str());

    if (mount_result != 0) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to mount ISO file");
        return;
    }

    // Check if install.wim exists
    if (!std::filesystem::exists(wim_path)) {
        system(("umount " + mount_point + " && rmdir " + mount_point).c_str());
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "No install.wim found in ISO");
        return;
    }

    // Open the WIM file using wimlib
    WIMStruct* wim = nullptr;
    int ret = wimlib_open_wim(wim_path.c_str(), 0, &wim);

    if (ret != WIMLIB_ERR_SUCCESS) {
        system(("umount " + mount_point + " && rmdir " + mount_point).c_str());
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to open WIM file");
        return;
    }

    // Get number of images by checking each index
    size_t num_images = 0;
    for (int i = 1; i <= 10; i++) { // Try up to 10 images
        const char* name = wimlib_get_image_name(wim, i);
        if (name && strlen(name) > 0) {
            num_images = i;
        } else {
            break;
        }
    }

    // Extract image information
    for (size_t i = 1; i <= num_images; i++) {
        wim_image_info image_info;

        // Get image name
        const char* name = wimlib_get_image_name(wim, i);
        if (name) {
            image_info.name = name;
        }

        // Get image description
        const char* description = wimlib_get_image_description(wim, i);
        if (description) {
            image_info.description = description;
        }

        // For display name and description, use the same as name/description
        image_info.display_name = image_info.name;
        image_info.display_description = image_info.description;

        // Only add if we have at least a name
        if (!image_info.name.empty()) {
            images.push_back(image_info);
        }
    }

    // Free WIM structure
    wimlib_free(wim);

    // Unmount and cleanup
    system(("umount " + mount_point + " && rmdir " + mount_point).c_str());

    if (images.empty()) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to scan WIM images or no images found");
        return;
    }

    // Convert to the expected format
    std::vector<std::string> windows_versions;
    for (const auto& image : images) {
        std::string version_info = image.display_name.empty() ? image.name : image.display_name;
        if (!image.display_description.empty()) {
            version_info += " - " + image.display_description;
        } else if (!image.description.empty()) {
            version_info += " - " + image.description;
        }
        windows_versions.push_back(version_info);
    }

    // Create result JSON
    nlohmann::json result;
    result["windows_versions"] = windows_versions;
    result["total_count"] = windows_versions.size();

    std::cout << "[Worker] WIM scan completed (ID: " << workload_id << "). Found "
              << windows_versions.size() << " versions." << std::endl;

    // Send completion status with result data
    m_ipc.send_workload_response(workload_id, workload_status::completed, result.dump());
}

void worker::install_vm(uint64_t workload_id, const nlohmann::json& params) {
    std::cout << "[Worker] Installing VM (ID: " << workload_id << ")..." << std::endl;
    std::cout << "[Worker] Parameters: " << params.dump() << std::endl;

    // Extract parameters
    std::string vm_name = params.value("vm_name", "LSWVM");
    std::string iso_path = params.value("iso_path", "");
    std::string windows_edition = params.value("windows_edition", "Home");
    std::string admin_username = params.value("admin_username", "lsw");
    std::string admin_password = params.value("admin_password", "");
    int memory_gb = params.value("memory_gb", 4);
    int cpu_cores = params.value("cpu_cores", 4);
    int disk_gb = params.value("disk_gb", 30);
    bool hardware_acceleration = params.value("hardware_acceleration", true);

    // Validate required parameters
    if (iso_path.empty()) {
        m_ipc.send_workload_response(workload_id, workload_status::error, "ISO path is required");
        return;
    }

    if (admin_password.empty()) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Admin password is required");
        return;
    }

    // Send in-progress status
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Connecting to libvirt daemon...");

    // Use application singleton's VM manager
    vm_manager& vm_mgr = application::instance().get_vm_manager();
    if (!vm_mgr.connect()) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to connect to libvirt: " + vm_mgr.get_last_error());
        return;
    }

    // Ensure network is available
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Ensuring network connectivity...");
    if (!vm_mgr.ensure_network_available("default")) {
        // Get detailed diagnostics
        std::string diagnostics = vm_mgr.get_network_diagnostics("default");
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to ensure network availability: " +
                                         vm_mgr.get_last_error() + "\n\n" + diagnostics);
        return;
    }

    // Check if VM already exists
    if (vm_mgr.vm_exists(vm_name)) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "VM '" + vm_name + "' already exists");
        return;
    }

    // Create separate autounattend ISO
    std::string autounattend_iso_path =
        "/tmp/" + vm_name + "_autounattend_" + std::to_string(getpid()) + ".iso";

    // Send progress update
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Creating autounattend ISO...");

    // Create autounattend configuration
    autounattend_manager::configuration autounattend_config;
    autounattend_config.computer_name = vm_name;
    autounattend_config.username = admin_username;
    autounattend_config.display_name = admin_username;
    autounattend_config.password = admin_password;

    // Generate autounattend.xml content using application singleton
    autounattend_manager& autounattend_mgr = application::instance().get_autounattend_manager();
    std::string autounattend_content = autounattend_mgr.generate_autounattend(autounattend_config);

    // Create temporary directory for autounattend ISO
    std::string temp_dir = "/tmp/lsw_autounattend_" + std::to_string(getpid());
    std::string autounattend_dir = temp_dir + "/autounattend";

    // Create directory
    std::string mkdir_cmd = "mkdir -p " + autounattend_dir;
    if (system(mkdir_cmd.c_str()) != 0) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to create temporary directory");
        return;
    }

    // Set up cleanup for temporary directory
    DEFER({
        std::cout << "[Worker] Cleaning up temporary autounattend files..." << std::endl;
        // Remove temporary directory
        system(("rm -rf " + temp_dir + " 2>/dev/null || true").c_str());
        std::cout << "[Worker] Temporary autounattend cleanup completed" << std::endl;
    });

    // Write autounattend.xml to the autounattend directory
    std::string autounattend_path = autounattend_dir + "/autounattend.xml";
    std::ofstream autounattend_file(autounattend_path);
    if (!autounattend_file.is_open()) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to create autounattend.xml file");
        return;
    }
    autounattend_file << autounattend_content;
    autounattend_file.close();

    // Create autounattend ISO using genisoimage
    std::string iso_cmd =
        "genisoimage -o " + autounattend_iso_path + " -V AUTOUNATTEND -J -r " + autounattend_dir;
    if (system(iso_cmd.c_str()) != 0) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to create autounattend ISO");
        return;
    }

    // Create VM configuration
    vm_config vm_config_obj;
    vm_config_obj.name = vm_name;
    vm_config_obj.iso_path = iso_path; // Use the original ISO
    vm_config_obj.windows_edition = windows_edition;
    vm_config_obj.admin_username = admin_username;
    vm_config_obj.admin_password = admin_password;
    vm_config_obj.memory_gb = memory_gb;
    vm_config_obj.cpu_cores = cpu_cores;
    vm_config_obj.disk_gb = disk_gb;
    vm_config_obj.hardware_acceleration = hardware_acceleration;
    vm_config_obj.use_autounattend = true; // We'll mount the autounattend ISO separately
    vm_config_obj.autounattend_iso_path = autounattend_iso_path; // Pass the autounattend ISO path
    vm_config_obj.virtio_iso_path =
        "/usr/share/virtio-win/virtio-win.iso"; // VirtIO guest tools ISO

    // Send progress update
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Creating VM '" + vm_name + "' with " + std::to_string(memory_gb) +
                                     "GB RAM and " + std::to_string(cpu_cores) + " CPU cores...");

    // Create the VM
    if (!vm_mgr.create_vm(vm_config_obj)) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to create VM: " + vm_mgr.get_last_error());
        return;
    }

    // Send progress update
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Starting VM '" + vm_name + "' for Windows installation...");

    // Start the VM
    if (!vm_mgr.start_vm(vm_name)) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to start VM: " + vm_mgr.get_last_error());
        return;
    }

    // Send progress update
    m_ipc.send_workload_response(
        workload_id, workload_status::in_progress,
        "Windows installation in progress... This may take 30-60 minutes.");

    // Monitor VM status during installation
    bool installation_complete = false;
    int check_count = 0;
    const int max_checks = 120; // Check for up to 2 hours (120 * 1 minute)

    while (!installation_complete && check_count < max_checks) {
        std::this_thread::sleep_for(std::chrono::minutes(10));
        check_count++;

        // Get VM status
        nlohmann::json vm_info = vm_mgr.get_vm_info(vm_name);
        if (vm_info.contains("error")) {
            m_ipc.send_workload_response(workload_id, workload_status::error,
                                         "Failed to monitor VM: " +
                                             vm_info["error"].get<std::string>());
            return;
        }

        std::string vm_state = vm_info.value("state", "unknown");

        // Send periodic progress updates
        if (check_count % 10 == 0) { // Every 10 minutes
            m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                         "Windows installation still in progress... (" +
                                             std::to_string(check_count) + " minutes elapsed)");
        }

        // Check if VM is still running (installation in progress)
        if (vm_state == "running") {
            continue; // Still installing
        } else if (vm_state == "shutoff") {
            // VM has shut down, installation might be complete
            installation_complete = true;
        } else {
            // VM crashed or in unexpected state
            m_ipc.send_workload_response(workload_id, workload_status::error,
                                         "VM installation failed - VM is in state: " + vm_state);
            return;
        }
    }

    if (check_count >= max_checks) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Windows installation timed out after 2 hours");
        return;
    }

    // Send progress update
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Windows installation completed! Starting VM...");

    // Start the VM again after installation
    if (!vm_mgr.start_vm(vm_name)) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to start VM after installation: " +
                                         vm_mgr.get_last_error());
        return;
    }

    std::cout << "[Worker] VM installation completed (ID: " << workload_id << ")" << std::endl;

    // Clean up temporary files now that installation is complete
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Cleaning up temporary files...");

    std::cout << "[Worker] Cleaning up autounattend ISO file..." << std::endl;
    system(("rm -f " + autounattend_iso_path + " 2>/dev/null || true").c_str());
    std::cout << "[Worker] Autounattend ISO cleanup completed" << std::endl;

    // Note: VirtIO ISO is typically a system file, so we don't remove it
    // The VM will continue to use it for driver access if needed
    std::cout << "[Worker] VirtIO drivers ISO preserved (system file)" << std::endl;

    // Send completion status with result data
    nlohmann::json result = {{"vm_name", vm_name},
                             {"iso_path", iso_path},
                             {"windows_edition", windows_edition},
                             {"admin_username", admin_username},
                             {"memory_gb", memory_gb},
                             {"cpu_cores", cpu_cores},
                             {"disk_gb", disk_gb},
                             {"hardware_acceleration", hardware_acceleration},
                             {"status", "installed_and_running"},
                             {"vm_id", vm_name},
                             {"installation_time_minutes", check_count}};

    m_ipc.send_workload_response(workload_id, workload_status::completed, result.dump());
}

void worker::get_vm_status(uint64_t workload_id, const nlohmann::json& params) {
    std::cout << "[Worker] Getting VM status (ID: " << workload_id << ")..." << std::endl;
    std::cout << "[Worker] Parameters: " << params.dump() << std::endl;

    // Extract parameters
    std::string vm_name = params.value("vm_name", "");

    if (vm_name.empty()) {
        m_ipc.send_workload_response(workload_id, workload_status::error, "VM name is required");
        return;
    }

    // Send progress update
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Getting status for VM '" + vm_name + "'...");

    // Use application singleton's VM manager
    vm_manager& vm_mgr = application::instance().get_vm_manager();
    if (!vm_mgr.connect()) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to connect to libvirt: " + vm_mgr.get_last_error());
        return;
    }

    // Get VM info
    nlohmann::json vm_info = vm_mgr.get_vm_info(vm_name);

    if (vm_info.contains("error")) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     vm_info["error"].get<std::string>());
        return;
    }

    std::cout << "[Worker] VM status retrieved (ID: " << workload_id << ")" << std::endl;
    m_ipc.send_workload_response(workload_id, workload_status::completed, vm_info.dump());
}

void worker::start_vm(uint64_t workload_id, const nlohmann::json& params) {
    std::cout << "[Worker] Starting VM (ID: " << workload_id << ")..." << std::endl;
    std::cout << "[Worker] Parameters: " << params.dump() << std::endl;

    // Extract parameters
    std::string vm_name = params.value("vm_name", "");

    if (vm_name.empty()) {
        m_ipc.send_workload_response(workload_id, workload_status::error, "VM name is required");
        return;
    }

    // Send progress update
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Starting VM '" + vm_name + "'...");

    // Use application singleton's VM manager
    vm_manager& vm_mgr = application::instance().get_vm_manager();
    if (!vm_mgr.connect()) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to connect to libvirt: " + vm_mgr.get_last_error());
        return;
    }

    // Start the VM
    if (!vm_mgr.start_vm(vm_name)) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to start VM: " + vm_mgr.get_last_error());
        return;
    }

    std::cout << "[Worker] VM started successfully (ID: " << workload_id << ")" << std::endl;

    nlohmann::json result = {{"vm_name", vm_name}, {"status", "running"}};
    m_ipc.send_workload_response(workload_id, workload_status::completed, result.dump());
}

void worker::stop_vm(uint64_t workload_id, const nlohmann::json& params) {
    std::cout << "[Worker] Stopping VM (ID: " << workload_id << ")..." << std::endl;
    std::cout << "[Worker] Parameters: " << params.dump() << std::endl;

    // Extract parameters
    std::string vm_name = params.value("vm_name", "");

    if (vm_name.empty()) {
        m_ipc.send_workload_response(workload_id, workload_status::error, "VM name is required");
        return;
    }

    // Send progress update
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Stopping VM '" + vm_name + "'...");

    // Use application singleton's VM manager
    vm_manager& vm_mgr = application::instance().get_vm_manager();
    if (!vm_mgr.connect()) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to connect to libvirt: " + vm_mgr.get_last_error());
        return;
    }

    // Stop the VM
    if (!vm_mgr.stop_vm(vm_name)) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to stop VM: " + vm_mgr.get_last_error());
        return;
    }

    std::cout << "[Worker] VM stopped successfully (ID: " << workload_id << ")" << std::endl;

    nlohmann::json result = {{"vm_name", vm_name}, {"status", "stopped"}};
    m_ipc.send_workload_response(workload_id, workload_status::completed, result.dump());
}

void worker::remove_vm(uint64_t workload_id, const nlohmann::json& params) {
    std::cout << "[Worker] Removing VM (ID: " << workload_id << ")..." << std::endl;
    std::cout << "[Worker] Parameters: " << params.dump() << std::endl;

    // Extract parameters
    std::string vm_name = params.value("vm_name", "");

    if (vm_name.empty()) {
        m_ipc.send_workload_response(workload_id, workload_status::error, "VM name is required");
        return;
    }

    // Send progress update
    m_ipc.send_workload_response(workload_id, workload_status::in_progress,
                                 "Removing VM '" + vm_name + "'...");

    // Use application singleton's VM manager
    vm_manager& vm_mgr = application::instance().get_vm_manager();
    if (!vm_mgr.connect()) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to connect to libvirt: " + vm_mgr.get_last_error());
        return;
    }

    // Remove the VM
    if (!vm_mgr.delete_vm(vm_name)) {
        m_ipc.send_workload_response(workload_id, workload_status::error,
                                     "Failed to remove VM: " + vm_mgr.get_last_error());
        return;
    }

    std::cout << "[Worker] VM removed successfully (ID: " << workload_id << ")" << std::endl;

    nlohmann::json result = {{"vm_name", vm_name}, {"status", "removed"}};
    m_ipc.send_workload_response(workload_id, workload_status::completed, result.dump());
}
