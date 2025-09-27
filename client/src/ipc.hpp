#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>

enum class workload_type {
    check_installed_apps,
    scan_wim_versions,
    install_vm,
    get_vm_status,
    start_vm,
    stop_vm,
    remove_vm,
};

enum class workload_status {
    in_progress,
    error,
    completed,
};

class ipc {
public:
    // Workload execution with callbacks
    using workload_success_callback = std::function<void(const nlohmann::json& result)>;
    using workload_error_callback = std::function<void(const std::string& message)>;
    using workload_progress_callback = std::function<void(const std::string& message)>;
    struct workload_callbacks {
        workload_success_callback on_complete = nullptr;
        workload_error_callback on_error = nullptr;
        workload_progress_callback on_progress = nullptr;
    };

    ipc();
    ~ipc();

    // Client operations
    bool create_server_socket(const std::string& socket_path);
    bool listen_for_connections(int backlog = 1);
    int accept_connection();
    void close_server_socket();

    // Worker operations
    bool connect_to_server(const std::string& socket_path);
    void close_worker_socket();

    // Message passing
    bool send_message(const std::string& message);
    std::string receive_message();
    uint64_t send_workload_request(workload_type workload,
                                   const nlohmann::json& params = nlohmann::json::object());
    std::tuple<uint64_t, workload_type, nlohmann::json> receive_workload_request();

    // Workload ID generation
    uint64_t generate_workload_id();

    // Worker response methods
    bool send_workload_response(uint64_t workload_id, workload_status status,
                                const std::string& message);
    std::tuple<uint64_t, workload_status, std::string> receive_workload_response();
    void execute_workload(workload_type workload,
                          const nlohmann::json& params = nlohmann::json::object(),
                          workload_success_callback on_complete = nullptr,
                          workload_error_callback on_error = nullptr,
                          workload_progress_callback on_progress = nullptr);
    void handle_workload_response(uint64_t workload_id, workload_status status,
                                  const std::string& message);

    // Common operations
    bool is_connected() const;
    int get_socket_fd() const;
    void set_socket(int socket_fd);

private:
    int m_socket_fd = -1;
    std::string m_socket_path;
    bool m_is_server = false;

    // Workload ID counter
    uint64_t m_workload_id_counter = 0;

    // Callback storage - map workload ID to callbacks
    std::unordered_map<uint64_t, workload_callbacks> m_workload_callbacks;
};
