#include "ipc.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <unistd.h>

ipc::ipc() = default;

ipc::~ipc() {
    if (m_is_server) {
        close_server_socket();
    } else {
        close_worker_socket();
    }
}

bool ipc::create_server_socket(const std::string& socket_path) {
    // Remove existing socket file
    unlink(socket_path.c_str());

    m_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_socket_fd == -1) {
        std::cerr << "[IPC] Failed to create socket" << std::endl;
        return false;
    }

    sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (bind(m_socket_fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "[IPC] Failed to bind socket" << std::endl;
        close(m_socket_fd);
        m_socket_fd = -1;
        return false;
    }

    m_socket_path = socket_path;
    m_is_server = true;
    std::cout << "[IPC] Server socket created and bound" << std::endl;
    return true;
}

bool ipc::listen_for_connections(int backlog) {
    if (m_socket_fd == -1 || !m_is_server) {
        std::cerr << "[IPC] Socket not initialized as server" << std::endl;
        return false;
    }

    if (listen(m_socket_fd, backlog) == -1) {
        std::cerr << "[IPC] Failed to listen on socket" << std::endl;
        return false;
    }

    std::cout << "[IPC] Socket listening for connections" << std::endl;
    return true;
}

int ipc::accept_connection() {
    if (m_socket_fd == -1 || !m_is_server) {
        std::cerr << "[IPC] Socket not initialized as server" << std::endl;
        return -1;
    }

    sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);
    int client_fd = accept(m_socket_fd, (sockaddr*)&addr, &addr_len);

    if (client_fd == -1) {
        std::cerr << "[IPC] Failed to accept connection" << std::endl;
        return -1;
    }

    std::cout << "[IPC] Connection accepted" << std::endl;
    return client_fd;
}

void ipc::close_server_socket() {
    if (m_socket_fd != -1) {
        close(m_socket_fd);
        m_socket_fd = -1;
    }

    if (!m_socket_path.empty()) {
        unlink(m_socket_path.c_str());
        m_socket_path.clear();
    }

    m_is_server = false;
    std::cout << "[IPC] Server socket closed" << std::endl;
}

bool ipc::connect_to_server(const std::string& socket_path) {
    m_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_socket_fd == -1) {
        std::cerr << "[IPC] Failed to create socket" << std::endl;
        return false;
    }

    sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (connect(m_socket_fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "[IPC] Failed to connect to server socket: " << strerror(errno) << std::endl;
        close(m_socket_fd);
        m_socket_fd = -1;
        return false;
    }

    m_socket_path = socket_path;
    m_is_server = false;
    std::cout << "[IPC] Connected to server socket" << std::endl;
    return true;
}

void ipc::close_worker_socket() {
    if (m_socket_fd != -1) {
        close(m_socket_fd);
        m_socket_fd = -1;
    }

    m_socket_path.clear();
    m_is_server = false;
    std::cout << "[IPC] Worker socket closed" << std::endl;
}

bool ipc::is_connected() const {
    return m_socket_fd != -1;
}

int ipc::get_socket_fd() const {
    return m_socket_fd;
}

void ipc::set_socket(int socket_fd) {
    if (m_socket_fd != -1 && m_socket_fd != socket_fd) {
        // Close the current socket if it's different from the new one
        close(m_socket_fd);
    }
    m_socket_fd = socket_fd;
    m_is_server = false; // Switch to worker mode
    std::cout << "[IPC] Switched to socket connection (FD: " << socket_fd << ")" << std::endl;
}

bool ipc::send_message(const std::string& message) {
    if (m_socket_fd == -1) {
        std::cerr << "[IPC] Socket not connected" << std::endl;
        return false;
    }

    // Send message length first
    size_t length = message.length();
    if (send(m_socket_fd, &length, sizeof(length), 0) == -1) {
        std::cerr << "[IPC] Failed to send message length: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return false;
    }

    // Send message content
    if (send(m_socket_fd, message.c_str(), length, 0) == -1) {
        std::cerr << "[IPC] Failed to send message content: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return false;
    }

    std::cout << "[IPC] Message sent: " << message << std::endl;
    return true;
}

std::string ipc::receive_message() {
    if (m_socket_fd == -1) {
        std::cerr << "[IPC] Socket not connected" << std::endl;
        return "";
    }

    // Receive message length first
    size_t length;
    if (recv(m_socket_fd, &length, sizeof(length), 0) == -1) {
        std::cerr << "[IPC] Failed to receive message length: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return "";
    }

    // Receive message content
    std::string message(length, '\0');
    if (recv(m_socket_fd, &message[0], length, 0) == -1) {
        std::cerr << "[IPC] Failed to receive message content: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return "";
    }

    std::cout << "[IPC] Message received: " << message << std::endl;
    return message;
}

uint64_t ipc::generate_workload_id() {
    return ++m_workload_id_counter;
}

uint64_t ipc::send_workload_request(workload_type workload, const nlohmann::json& params) {
    if (m_socket_fd == -1) {
        std::cerr << "[IPC] Socket not connected" << std::endl;
        return 0;
    }

    // Check socket state before sending
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(m_socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
        std::cerr << "[IPC] Failed to get socket error: " << strerror(errno) << std::endl;
    } else if (error != 0) {
        std::cerr << "[IPC] Socket has error: " << strerror(error) << " (error: " << error << ")"
                  << std::endl;
        return 0;
    }

    // Generate workload ID
    uint64_t workload_id = generate_workload_id();

    // Send workload ID first
    if (send(m_socket_fd, &workload_id, sizeof(workload_id), 0) == -1) {
        std::cerr << "[IPC] Failed to send workload ID: " << strerror(errno) << " (errno: " << errno
                  << ")" << std::endl;
        return 0;
    }

    // Send workload type
    uint8_t workload_byte = static_cast<uint8_t>(workload);
    if (send(m_socket_fd, &workload_byte, sizeof(workload_byte), 0) == -1) {
        std::cerr << "[IPC] Failed to send workload request: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return 0;
    }

    // Send parameters as JSON string
    std::string params_str = params.dump();
    size_t params_length = params_str.length();

    // Send parameters length
    if (send(m_socket_fd, &params_length, sizeof(params_length), 0) == -1) {
        std::cerr << "[IPC] Failed to send parameters length: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return 0;
    }

    // Send parameters content
    if (send(m_socket_fd, params_str.c_str(), params_length, 0) == -1) {
        std::cerr << "[IPC] Failed to send parameters content: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return 0;
    }

    std::cout << "[IPC] Workload request sent - ID: " << workload_id
              << ", Type: " << static_cast<int>(workload_byte) << ", Params: " << params_str
              << std::endl;
    return workload_id;
}

std::tuple<uint64_t, workload_type, nlohmann::json> ipc::receive_workload_request() {
    if (m_socket_fd == -1) {
        std::cerr << "[IPC] Socket not connected" << std::endl;
        return {0, static_cast<workload_type>(-1), nlohmann::json::object()};
    }

    // Receive workload ID first
    uint64_t workload_id;
    if (recv(m_socket_fd, &workload_id, sizeof(workload_id), 0) == -1) {
        std::cerr << "[IPC] Failed to receive workload ID: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return {0, static_cast<workload_type>(-1), nlohmann::json::object()};
    }

    // Receive workload type
    uint8_t workload_byte;
    if (recv(m_socket_fd, &workload_byte, sizeof(workload_byte), 0) == -1) {
        std::cerr << "[IPC] Failed to receive workload request: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return {0, static_cast<workload_type>(-1), nlohmann::json::object()};
    }

    // Receive parameters length
    size_t params_length;
    if (recv(m_socket_fd, &params_length, sizeof(params_length), 0) == -1) {
        std::cerr << "[IPC] Failed to receive parameters length: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return {0, static_cast<workload_type>(-1), nlohmann::json::object()};
    }

    // Receive parameters content
    std::string params_str(params_length, '\0');
    if (recv(m_socket_fd, &params_str[0], params_length, 0) == -1) {
        std::cerr << "[IPC] Failed to receive parameters content: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return {0, static_cast<workload_type>(-1), nlohmann::json::object()};
    }

    // Parse JSON parameters
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(params_str);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[IPC] Failed to parse parameters JSON: " << e.what() << std::endl;
        params = nlohmann::json::object();
    }

    std::cout << "[IPC] Workload request received - ID: " << workload_id
              << ", Type: " << static_cast<int>(workload_byte) << ", Params: " << params_str
              << std::endl;
    return {workload_id, static_cast<workload_type>(workload_byte), params};
}

bool ipc::send_workload_response(uint64_t workload_id, workload_status status,
                                 const std::string& message) {
    if (m_socket_fd == -1) {
        std::cerr << "[IPC] Socket not connected" << std::endl;
        return false;
    }

    // Send workload ID first
    if (send(m_socket_fd, &workload_id, sizeof(workload_id), 0) == -1) {
        std::cerr << "[IPC] Failed to send workload ID: " << strerror(errno) << " (errno: " << errno
                  << ")" << std::endl;
        return false;
    }

    // Send status byte
    uint8_t status_byte = static_cast<uint8_t>(status);
    if (send(m_socket_fd, &status_byte, sizeof(status_byte), 0) == -1) {
        std::cerr << "[IPC] Failed to send status byte: " << strerror(errno) << " (errno: " << errno
                  << ")" << std::endl;
        return false;
    }

    // Send message length
    size_t length = message.length();
    if (send(m_socket_fd, &length, sizeof(length), 0) == -1) {
        std::cerr << "[IPC] Failed to send message length: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return false;
    }

    // Send message content
    if (send(m_socket_fd, message.c_str(), length, 0) == -1) {
        std::cerr << "[IPC] Failed to send message content: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return false;
    }

    std::cout << "[IPC] Workload response sent - ID: " << workload_id
              << ", Status: " << static_cast<int>(status_byte) << ", Message: " << message
              << std::endl;
    return true;
}

std::tuple<uint64_t, workload_status, std::string> ipc::receive_workload_response() {
    if (m_socket_fd == -1) {
        std::cerr << "[IPC] Socket not connected" << std::endl;
        return {0, static_cast<workload_status>(-1), ""};
    }

    // Receive workload ID first
    uint64_t workload_id;
    if (recv(m_socket_fd, &workload_id, sizeof(workload_id), 0) == -1) {
        std::cerr << "[IPC] Failed to receive workload ID: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return {0, static_cast<workload_status>(-1), ""};
    }

    // Receive status byte
    uint8_t status_byte;
    if (recv(m_socket_fd, &status_byte, sizeof(status_byte), 0) == -1) {
        std::cerr << "[IPC] Failed to receive status byte: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return {0, static_cast<workload_status>(-1), ""};
    }

    // Receive message length
    size_t length;
    if (recv(m_socket_fd, &length, sizeof(length), 0) == -1) {
        std::cerr << "[IPC] Failed to receive message length: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return {0, static_cast<workload_status>(-1), ""};
    }

    // Receive message content
    std::string message(length, '\0');
    if (recv(m_socket_fd, &message[0], length, 0) == -1) {
        std::cerr << "[IPC] Failed to receive message content: " << strerror(errno)
                  << " (errno: " << errno << ")" << std::endl;
        return {0, static_cast<workload_status>(-1), ""};
    }

    workload_status status = static_cast<workload_status>(status_byte);
    std::cout << "[IPC] Workload response received - ID: " << workload_id
              << ", Status: " << static_cast<int>(status_byte) << ", Message: " << message
              << std::endl;
    return {workload_id, status, message};
}

void ipc::execute_workload(workload_type workload, const nlohmann::json& params,
                           workload_success_callback on_complete, workload_error_callback on_error,
                           workload_progress_callback on_progress) {
    // Send workload request (IPC will generate ID)
    uint64_t workload_id = send_workload_request(workload, params);
    if (workload_id == 0) {
        std::cerr << "[IPC] Failed to send workload request" << std::endl;
        if (on_error) {
            on_error("Failed to send workload request");
        }
        return;
    }

    // Store callbacks for this workload ID
    m_workload_callbacks[workload_id] = {on_complete, on_error, on_progress};

    std::cout << "[IPC] Workload request sent (ID: " << workload_id << ")" << std::endl;
}

void ipc::handle_workload_response(uint64_t workload_id, workload_status status,
                                   const std::string& message) {
    // Find the callbacks for this workload ID
    auto it = m_workload_callbacks.find(workload_id);
    if (it != m_workload_callbacks.end()) {
        // Handle callbacks and log the response
        switch (status) {
        case workload_status::in_progress:
            std::cout << "[IPC] Workload " << workload_id << " in progress: " << message
                      << std::endl;
            if (it->second.on_progress) {
                it->second.on_progress(message);
            }
            break;
        case workload_status::completed:
            std::cout << "[IPC] Workload " << workload_id << " completed: " << message << std::endl;
            if (it->second.on_complete) {
                try {
                    // Parse JSON result
                    nlohmann::json result = nlohmann::json::parse(message);
                    it->second.on_complete(result);
                } catch (const nlohmann::json::parse_error& e) {
                    std::cerr << "[IPC] Failed to parse JSON result: " << e.what() << std::endl;
                    // Call error callback with parse error
                    if (it->second.on_error) {
                        it->second.on_error("Failed to parse JSON result: " +
                                            std::string(e.what()));
                    }
                }
            }
            // Remove the callback entry since the workload is complete
            m_workload_callbacks.erase(it);
            break;
        case workload_status::error:
            std::cout << "[IPC] Workload " << workload_id << " error: " << message << std::endl;
            if (it->second.on_error) {
                it->second.on_error(message);
            }
            // Remove the callback entry since the workload failed
            m_workload_callbacks.erase(it);
            break;
        }
    } else {
        std::cout << "[IPC] Received response for unknown workload ID: " << workload_id
                  << std::endl;
    }
}
