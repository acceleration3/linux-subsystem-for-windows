#pragma once

#include <optional>
#include <glib.h>
#include <gtk-4.0/gtk/gtk.h>
#include <libadwaita-1/adwaita.h>

#include "autounattend_manager.hpp"
#include "installer_window.hpp"
#include "ipc.hpp"
#include "vm_manager.hpp"
#include "worker.hpp"

enum class application_mode {
    client,
    worker,
};

class application {
public:
    static application& instance() {
        static application app;
        return app;
    }

    int run(int argc, char** argv);

    ipc& get_ipc() {
        return m_ipc;
    }

    vm_manager& get_vm_manager() {
        return m_vm_manager;
    }

    autounattend_manager& get_autounattend_manager() {
        return m_autounattend_manager;
    }

    // Remove copy/move constructors
    application(const application&) = delete;
    application& operator=(const application&) = delete;
    application(application&&) = delete;
    application& operator=(application&&) = delete;

private:
    application();
    ~application();

    installer_window m_installer_window;
    AdwApplication* m_app = nullptr;
    ipc m_ipc;
    std::optional<worker> m_worker;
    vm_manager m_vm_manager;
    autounattend_manager m_autounattend_manager;

    int run_worker_mode();
    int run_client_mode(const char* app_path);
    bool launch_worker(const char* app_path);

    // IPC monitoring
    void setup_ipc_monitoring();
    static gboolean on_ipc_data_available(GIOChannel* source, GIOCondition condition,
                                          gpointer user_data);

    static void on_activate(AdwApplication* app, gpointer user_data);
    static void on_startup(AdwApplication* app, gpointer user_data);
};