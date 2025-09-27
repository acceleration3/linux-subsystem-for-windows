#include "application.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include <adwaita.h>
#include <glib-unix.h>
#include <gtk/gtk.h>

constexpr const char* SOCKET_PATH = "/tmp/lsw.sock";

// Forward declaration for the resource function
extern "C" GResource* resources_get_resource(void);

application::application() {
    // Create the AdwApplication
    m_app = adw_application_new("com.accel.lsw", G_APPLICATION_DEFAULT_FLAGS);

    // Connect signals
    g_signal_connect(m_app, "activate", G_CALLBACK(on_activate), nullptr);
    g_signal_connect(m_app, "startup", G_CALLBACK(on_startup), nullptr);
}

application::~application() {
    if (m_app) {
        g_object_unref(m_app);
    }
}

int application::run(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--worker") {
        return run_worker_mode();
    } else {
        return run_client_mode(argv[0]);
    }
    return 0;
}

int application::run_worker_mode() {
    m_worker.emplace();
    return m_worker->run(SOCKET_PATH);
}

int application::run_client_mode(const char* app_path) {
    if (!m_ipc.create_server_socket(SOCKET_PATH)) {
        std::cerr << "[Client] Failed to create server socket" << std::endl;
        return 1;
    }
    if (!m_ipc.listen_for_connections(1)) {
        std::cerr << "[Client] Failed to listen for connections" << std::endl;
        return 1;
    }
    if (!launch_worker(app_path)) {
        std::cerr << "[Client] Failed to launch worker process" << std::endl;
        return 1;
    }

    // Give the worker a moment to connect
    std::cout << "[Client] Waiting for worker to connect..." << std::endl;
    sleep(1);

    int worker_fd = m_ipc.accept_connection();
    if (worker_fd == -1) {
        std::cerr << "[Client] Failed to accept worker connection" << std::endl;
        return 1;
    }
    std::cout << "[Client] Worker connected" << std::endl;

    // Switch IPC to use the worker connection instead of server socket
    m_ipc.set_socket(worker_fd);

    // Setup IPC monitoring for the GTK main loop
    setup_ipc_monitoring();

    return g_application_run(G_APPLICATION(m_app), 0, nullptr);
}

bool application::launch_worker(const char* app_path) {
    std::cout << "[Client] Starting worker in root mode" << std::endl;

    // Use fork and exec to launch worker in background
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - exec the worker
        execl("/usr/bin/pkexec", "pkexec", app_path, "--worker", nullptr);
        std::cerr << "[Client] Failed to exec worker: " << strerror(errno) << std::endl;
        exit(1);
    } else if (pid > 0) {
        // Parent process - worker launched successfully
        std::cout << "[Client] Worker started (PID: " << pid << ")" << std::endl;
        return true;
    } else {
        // Fork failed
        std::cerr << "[Client] Failed to fork worker process: " << strerror(errno) << std::endl;
        return false;
    }
}

void application::on_startup(AdwApplication* app, gpointer user_data) {
    // Register the GResource
    GResource* resource = resources_get_resource();
    g_resources_register(resource);
}

void application::on_activate(AdwApplication* app, gpointer user_data) {
    application& self = application::instance();

    // Load and show the installer window
    self.m_installer_window.load(GTK_APPLICATION(app));
    self.m_installer_window.show();

    // Load and show the main window
    // self.m_main_window.load(GTK_APPLICATION(app), "/com/accel/lsw/ui/main.ui");
    // self.m_main_window.show();
}

void application::setup_ipc_monitoring() {
    int socket_fd = m_ipc.get_socket_fd();
    if (socket_fd == -1) {
        std::cerr << "[Client] Cannot setup IPC monitoring - no socket" << std::endl;
        return;
    }

    // Create a GIOChannel for the socket
    GIOChannel* channel = g_io_channel_unix_new(socket_fd);
    g_io_channel_set_encoding(channel, nullptr, nullptr);
    g_io_channel_set_buffered(channel, false);

    // Add the socket to the main loop for monitoring
    g_io_add_watch(channel, G_IO_IN, on_ipc_data_available, nullptr);

    std::cout << "[Client] IPC monitoring setup complete" << std::endl;
}

gboolean application::on_ipc_data_available(GIOChannel* source, GIOCondition condition,
                                            gpointer user_data) {
    application& self = application::instance();

    if (condition & G_IO_IN) {
        // Data is available on the socket
        auto [workload_id, status, message] = self.m_ipc.receive_workload_response();

        if (status != static_cast<workload_status>(-1)) {
            // Delegate response handling to IPC class
            self.m_ipc.handle_workload_response(workload_id, status, message);
        }
    }

    if (condition & (G_IO_HUP | G_IO_ERR)) {
        std::cout << "[Client] IPC connection closed or error occurred" << std::endl;
        return G_SOURCE_REMOVE; // Stop monitoring
    }

    return G_SOURCE_CONTINUE; // Continue monitoring
}
