#pragma once

#include <functional>
#include <memory>
#include <string>
#include <adwaita.h>
#include <gtk-4.0/gtk/gtk.h>
#include "net/microsoft_interface.hpp"
#include "net/multipart_transfer.hpp"

struct installer_window_data {
    std::string iso_path;
    std::string download_path;
    bool use_download = true; // true for download, false for select existing

    // VM settings
    int memory_gb = 4;
    int cpu_cores = 4;
    int disk_gb = 30;
    std::string admin_username = "lsw";
    std::string admin_password;
    bool hardware_acceleration = true;
};

// Forward declaration
class installer_window;

struct page_properties {
    bool back_enabled;
    bool next_enabled;
    std::function<void()> page_action;

    page_properties(bool back = true, bool next = true, std::function<void()> action = nullptr)
        : back_enabled(back), next_enabled(next), page_action(action) {}
};

class installer_window {
public:
    installer_window();
    ~installer_window();

    installer_window(const installer_window&) = delete;
    installer_window& operator=(const installer_window&) = delete;

    installer_window(installer_window&&) = delete;
    installer_window& operator=(installer_window&&) = delete;

    bool load(GtkApplication* app);
    void show();

    using finish_callback_t = std::function<void(const installer_window_data& data)>;
    void set_finish_callback(finish_callback_t callback) {
        m_finish_callback = std::move(callback);
    }

private:
    int m_current_page = 0;
    GtkWindow* m_window = nullptr;
    GtkBuilder* m_builder = nullptr;
    GtkButton* m_iso_browse_button = nullptr;
    GtkLabel* m_iso_path_label = nullptr;
    GtkButton* m_next_button = nullptr;
    GtkButton* m_back_button = nullptr;
    GtkTextView* m_textview = nullptr;
    GtkCheckButton* m_download_radio = nullptr;
    GtkCheckButton* m_select_radio = nullptr;
    GtkLabel* m_download_path_label = nullptr;
    GtkButton* m_download_browse_button = nullptr;
    GtkSpinner* m_loading_spinner = nullptr;
    GtkImage* m_banned_icon = nullptr;
    GtkLabel* m_loading_label = nullptr;
    GtkProgressBar* m_download_progress = nullptr;
    GtkLabel* m_download_status_label = nullptr;
    AdwComboRow* m_windows_edition_combo = nullptr;
    AdwCarousel* m_carousel = nullptr;

    // VM settings UI elements
    AdwSpinRow* m_memory_spinner = nullptr;
    AdwSpinRow* m_storage_spinner = nullptr;
    AdwSpinRow* m_cpu_spinner = nullptr;
    AdwEntryRow* m_admin_username_entry = nullptr;
    AdwPasswordEntryRow* m_admin_password_entry = nullptr;
    GtkButton* m_install_button = nullptr;
    GtkCheckButton* m_hardware_accel_check = nullptr;
    finish_callback_t m_finish_callback;
    installer_window_data m_data;

    // Page properties
    std::vector<page_properties> m_page_config;

    // Microsoft interface and download components
    std::unique_ptr<microsoft_interface> m_microsoft_interface;
    std::unique_ptr<multipart_transfer> m_downloader;

    void initialize_page_config();
    bool is_page_valid(int page) const;
    void update_navigation_state();
    int get_total_pages() const;
    void start_wim_scan_async();
    void wim_scan_thread(installer_window* self);
    void perform_page_action(int page);
    void append_progress_message(const std::string& message);
    void update_iso_source_ui();
    void show_download_progress(bool show);
    void show_banned_state(bool show);
    void populate_windows_editions(const std::vector<std::string>& editions);
    std::string get_selected_windows_edition() const;
    void start_iso_download();
    void on_download_progress(const multipart_transfer::progress_info& info);
    void on_download_complete(bool success, const std::string& error);
    void start_vm_installation();
    void collect_vm_settings();

    static void on_next_button_clicked(GtkButton* button, gpointer user_data);
    static void on_back_button_clicked(GtkButton* button, gpointer user_data);
    static void on_iso_browse_button_clicked(GtkButton* button, gpointer user_data);
    static void on_file_dialog_response(GObject* source_object, GAsyncResult* result,
                                        gpointer user_data);
    static void on_download_browse_button_clicked(GtkButton* button, gpointer user_data);
    static void on_download_radio_toggled(GtkCheckButton* button, gpointer user_data);
    static void on_select_radio_toggled(GtkCheckButton* button, gpointer user_data);
    static void on_download_folder_dialog_response(GObject* source_object, GAsyncResult* result,
                                                   gpointer user_data);
    static void on_install_button_clicked(GtkButton* button, gpointer user_data);
};