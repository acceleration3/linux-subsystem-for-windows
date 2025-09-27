#include "gio/gio.h"
#include "installer_window.hpp"

#include <chrono>
#include <ctime>
#include <iostream>

#include <gtk-4.0/gtk/gtk.h>
#include <nlohmann/json.hpp>
#include <wimlib.h>
#include "application.hpp"

installer_window::installer_window() : m_window(nullptr), m_builder(nullptr) {}

installer_window::~installer_window() {
    if (m_builder) {
        g_object_unref(m_builder);
    }
    if (m_window) {
        g_object_unref(m_window);
    }
}

bool installer_window::load(GtkApplication* app) {
    GtkBuilder* builder = gtk_builder_new_from_resource("/com/accel/lsw/ui/installer.ui");
    if (!builder) {
        std::cerr << "Failed to create builder" << std::endl;
        return false;
    }

    m_window = GTK_WINDOW(gtk_builder_get_object(builder, "window"));
    if (!m_window) {
        g_object_unref(builder);
        std::cerr << "Failed to get window" << std::endl;
        return false;
    }

    m_iso_browse_button = GTK_BUTTON(gtk_builder_get_object(builder, "iso_browse_button"));
    m_iso_path_label = GTK_LABEL(gtk_builder_get_object(builder, "iso_path_label"));
    m_next_button = GTK_BUTTON(gtk_builder_get_object(builder, "next_button"));
    m_back_button = GTK_BUTTON(gtk_builder_get_object(builder, "back_button"));
    m_textview = GTK_TEXT_VIEW(gtk_builder_get_object(builder, "install_log"));
    m_download_radio = GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "download_radio"));
    m_select_radio = GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "select_radio"));
    m_download_path_label = GTK_LABEL(gtk_builder_get_object(builder, "download_path_label"));
    m_download_browse_button =
        GTK_BUTTON(gtk_builder_get_object(builder, "download_browse_button"));
    m_loading_spinner = GTK_SPINNER(gtk_builder_get_object(builder, "loading_spinner"));
    m_banned_icon = GTK_IMAGE(gtk_builder_get_object(builder, "banned_icon"));
    m_loading_label = GTK_LABEL(gtk_builder_get_object(builder, "loading_label"));
    m_download_progress = GTK_PROGRESS_BAR(gtk_builder_get_object(builder, "download_progress"));
    m_download_status_label = GTK_LABEL(gtk_builder_get_object(builder, "download_status_label"));
    m_windows_edition_combo =
        ADW_COMBO_ROW(gtk_builder_get_object(builder, "windows_edition_combo"));
    m_carousel = ADW_CAROUSEL(gtk_builder_get_object(builder, "carousel"));

    // Get VM settings UI elements
    m_memory_spinner = ADW_SPIN_ROW(gtk_builder_get_object(builder, "memory_spinner"));
    m_storage_spinner = ADW_SPIN_ROW(gtk_builder_get_object(builder, "storage_spinner"));
    m_cpu_spinner = ADW_SPIN_ROW(gtk_builder_get_object(builder, "cpu_spinner"));
    m_admin_username_entry = ADW_ENTRY_ROW(gtk_builder_get_object(builder, "admin_username_entry"));
    m_admin_password_entry =
        ADW_PASSWORD_ENTRY_ROW(gtk_builder_get_object(builder, "admin_password_entry"));
    m_install_button = GTK_BUTTON(gtk_builder_get_object(builder, "install_button"));
    m_hardware_accel_check =
        GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "hardware_accel_check"));

    g_signal_connect(m_iso_browse_button, "clicked", G_CALLBACK(on_iso_browse_button_clicked),
                     this);
    g_signal_connect(m_next_button, "clicked", G_CALLBACK(on_next_button_clicked), this);
    g_signal_connect(m_back_button, "clicked", G_CALLBACK(on_back_button_clicked), this);
    g_signal_connect(m_download_browse_button, "clicked",
                     G_CALLBACK(on_download_browse_button_clicked), this);
    g_signal_connect(m_download_radio, "toggled", G_CALLBACK(on_download_radio_toggled), this);
    g_signal_connect(m_select_radio, "toggled", G_CALLBACK(on_select_radio_toggled), this);
    g_signal_connect(m_install_button, "clicked", G_CALLBACK(on_install_button_clicked), this);

    gtk_window_set_application(m_window, app);

    // Initialize data
    m_data.download_path = g_get_home_dir();
    m_data.download_path += "/Downloads";
    gtk_label_set_text(m_download_path_label, m_data.download_path.c_str());

    // Set initial UI state
    update_iso_source_ui();

    // Initialize Microsoft interface
    m_microsoft_interface = std::make_unique<microsoft_interface>();
    m_downloader = std::make_unique<multipart_transfer>();

    // Initialize page configuration
    initialize_page_config();

    return true;
}

void installer_window::initialize_page_config() {
    m_page_config.emplace_back(false, false, nullptr);
    m_page_config.emplace_back(false, false, [this]() { start_wim_scan_async(); });
    m_page_config.emplace_back(true, true, nullptr);
    m_page_config.emplace_back(true, false, nullptr);
}

void installer_window::show() {
    gtk_window_present(m_window);
    m_current_page = 0;
    update_navigation_state();
    perform_page_action(m_current_page);
}

bool installer_window::is_page_valid(int page) const {
    if (page < 0 || page >= static_cast<int>(m_page_config.size())) {
        return false;
    }

    // Page 0 has special validation logic
    if (page == 0) {
        if (m_data.use_download) {
            return !m_data.download_path.empty();
        } else {
            return !m_data.iso_path.empty();
        }
    }

    // For other pages, check if next button should be enabled
    return m_page_config[page].next_enabled;
}

void installer_window::update_navigation_state() {
    if (m_current_page < 0 || m_current_page >= static_cast<int>(m_page_config.size())) {
        return;
    }

    const auto& page_config = m_page_config[m_current_page];
    bool at_last = (m_current_page == static_cast<int>(m_page_config.size()) - 1);

    gtk_widget_set_sensitive(GTK_WIDGET(m_back_button), page_config.back_enabled);
    gtk_button_set_label(m_next_button, at_last ? "Finish" : "Next");
    gtk_widget_set_sensitive(GTK_WIDGET(m_next_button), is_page_valid(m_current_page));
}

void installer_window::on_next_button_clicked(GtkButton* button, gpointer user_data) {
    installer_window* self = static_cast<installer_window*>(user_data);

    int next_index = self->m_current_page + 1;
    if (next_index >= static_cast<int>(self->m_page_config.size())) {
        // Already at last page: treat as finish action (no-op here)
        return;
    }

    GtkWidget* target = gtk_widget_get_first_child(GTK_WIDGET(self->m_carousel));
    for (int i = 0; i < next_index && target != nullptr; ++i) {
        target = gtk_widget_get_next_sibling(target);
    }

    if (target != nullptr) {
        adw_carousel_scroll_to(ADW_CAROUSEL(self->m_carousel), target, 1);
        self->m_current_page = next_index;
        self->perform_page_action(self->m_current_page);
        self->update_navigation_state();
    }
}

void installer_window::on_back_button_clicked(GtkButton* button, gpointer user_data) {
    installer_window* self = static_cast<installer_window*>(user_data);

    // Do nothing if already at the first page
    if (self->m_current_page <= 0) {
        return;
    }

    int prev_index = self->m_current_page - 1;
    GtkWidget* target = gtk_widget_get_first_child(GTK_WIDGET(self->m_carousel));
    for (int i = 0; i < prev_index && target != nullptr; ++i) {
        target = gtk_widget_get_next_sibling(target);
    }

    if (target != nullptr) {
        adw_carousel_scroll_to(ADW_CAROUSEL(self->m_carousel), target, 1);
        self->m_current_page = prev_index;
        self->perform_page_action(self->m_current_page);
        self->update_navigation_state();
    }
}

void installer_window::on_iso_browse_button_clicked(GtkButton* button, gpointer user_data) {
    installer_window* self = static_cast<installer_window*>(user_data);

    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select the Windows ISO File");
    gtk_file_dialog_set_accept_label(dialog, "Select");

    // Create and apply a filter for ISO files
    GtkFileFilter* iso_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(iso_filter, "ISO images");
    gtk_file_filter_add_suffix(iso_filter, "iso");
    gtk_file_filter_add_mime_type(iso_filter, "application/x-iso9660-image");

    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, iso_filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, iso_filter);
    g_object_unref(filters);

    gtk_file_dialog_open(dialog, GTK_WINDOW(self->m_window), nullptr, on_file_dialog_response,
                         self);

    g_object_unref(dialog);
}

void installer_window::on_file_dialog_response(GObject* source_object, GAsyncResult* result,
                                               gpointer user_data) {
    installer_window* self = static_cast<installer_window*>(user_data);

    GtkFileDialog* dialog = GTK_FILE_DIALOG(source_object);
    GFile* file = gtk_file_dialog_open_finish(dialog, result, nullptr);

    if (file) {
        char* file_path = g_file_get_path(file);
        if (file_path) {
            gtk_label_set_text(GTK_LABEL(self->m_iso_path_label), file_path);
            self->m_data.iso_path = file_path;
            self->update_navigation_state();
        }
        g_free(file_path);
        g_object_unref(file);
    }
}

void installer_window::perform_page_action(int page) {
    if (page < 0 || page >= static_cast<int>(m_page_config.size())) {
        return;
    }

    if (m_page_config[page].page_action) {
        m_page_config[page].page_action();
    }
}

void installer_window::start_wim_scan_async() {
    // Disable navigation while scanning
    gtk_widget_set_sensitive(GTK_WIDGET(m_next_button), false);
    gtk_widget_set_sensitive(GTK_WIDGET(m_back_button), false);

    // Show appropriate UI and start process
    show_download_progress(m_data.use_download);
    if (m_loading_label && GTK_IS_LABEL(m_loading_label)) {
        gtk_label_set_text(m_loading_label,
                           m_data.use_download ? "Downloading ISO..." : "Scanning Windows ISO...");
    }

    if (m_data.use_download) {
        start_iso_download();
    } else {
        wim_scan_thread(this);
    }
}

void installer_window::wim_scan_thread(installer_window* self) {
    // Execute WIM scan workload
    nlohmann::json params = {{"iso_path", self->m_data.iso_path}};

    application::instance().get_ipc().execute_workload(
        workload_type::scan_wim_versions, params,
        [self](const nlohmann::json& result) {
            // WIM scan completed successfully
            if (!self || !self->m_window || !GTK_IS_WINDOW(self->m_window)) {
                return;
            }

            // Update loading message with scan results
            int count = result["total_count"].get<int>();
            std::string message =
                "Found " + std::to_string(count) + " Windows version(s). Proceeding to settings...";
            if (self->m_loading_label && GTK_IS_LABEL(self->m_loading_label)) {
                gtk_label_set_text(self->m_loading_label, message.c_str());
            }

            // Populate the Windows editions dropdown
            std::vector<std::string> editions;
            for (const auto& version : result["windows_versions"]) {
                editions.push_back(version.get<std::string>());
            }
            self->populate_windows_editions(editions);

            // Update navigation state using page properties
            self->update_navigation_state();

            // Auto-advance to next page after a short delay
            g_timeout_add(
                1000,
                [](gpointer ud) -> gboolean {
                    installer_window* s = static_cast<installer_window*>(ud);
                    if (s && s->m_window && GTK_IS_WINDOW(s->m_window)) {
                        s->on_next_button_clicked(nullptr, s);
                    }
                    return G_SOURCE_REMOVE;
                },
                self);
        },
        [self](const std::string& error) {
            // WIM scan failed
            if (!self || !self->m_window || !GTK_IS_WINDOW(self->m_window)) {
                return;
            }

            // Show error message
            if (self->m_loading_label && GTK_IS_LABEL(self->m_loading_label)) {
                gtk_label_set_text(self->m_loading_label, ("WIM scan failed: " + error).c_str());
            }

            // Update navigation state using page properties
            self->update_navigation_state();
        },
        [self](const std::string& progress) {
            // WIM scan progress update
            if (!self || !self->m_window || !GTK_IS_WINDOW(self->m_window)) {
                return;
            }

            // Update loading message with progress
            if (self->m_loading_label && GTK_IS_LABEL(self->m_loading_label)) {
                gtk_label_set_text(self->m_loading_label, progress.c_str());
            }
        });
}

void installer_window::append_progress_message(const std::string& message) {
    if (!m_textview) {
        return;
    }

    GtkTextBuffer* buffer = gtk_text_view_get_buffer(m_textview);
    if (!buffer) {
        return;
    }

    // Get current text
    GtkTextIter end_iter;
    gtk_text_buffer_get_end_iter(buffer, &end_iter);

    // Add timestamp and message
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);

    char timestamp[64];
    std::strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", &tm);

    std::string full_message = timestamp + message + "\n";

    // Insert the message
    gtk_text_buffer_insert(buffer, &end_iter, full_message.c_str(), -1);

    // Auto-scroll to bottom
    gtk_text_view_scroll_to_mark(m_textview, gtk_text_buffer_get_insert(buffer), 0.0, TRUE, 0.0,
                                 1.0);
}

void installer_window::update_iso_source_ui() {
    // Get the parent containers for the preference groups
    GtkWidget* download_group = gtk_widget_get_parent(GTK_WIDGET(m_download_path_label));
    while (download_group && !ADW_IS_PREFERENCES_GROUP(download_group)) {
        download_group = gtk_widget_get_parent(download_group);
    }

    GtkWidget* iso_group = gtk_widget_get_parent(GTK_WIDGET(m_iso_path_label));
    while (iso_group && !ADW_IS_PREFERENCES_GROUP(iso_group)) {
        iso_group = gtk_widget_get_parent(iso_group);
    }

    if (download_group)
        gtk_widget_set_visible(download_group, m_data.use_download);
    if (iso_group)
        gtk_widget_set_visible(iso_group, !m_data.use_download);
}

void installer_window::on_download_browse_button_clicked(GtkButton* button, gpointer user_data) {
    installer_window* self = static_cast<installer_window*>(user_data);

    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Download Folder");
    gtk_file_dialog_set_accept_label(dialog, "Select");

    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(self->m_window), nullptr,
                                  on_download_folder_dialog_response, self);

    g_object_unref(dialog);
}

void installer_window::on_download_radio_toggled(GtkCheckButton* button, gpointer user_data) {
    installer_window* self = static_cast<installer_window*>(user_data);
    if (gtk_check_button_get_active(button)) {
        self->m_data.use_download = true;
        self->update_iso_source_ui();
        self->update_navigation_state();
    }
}

void installer_window::on_select_radio_toggled(GtkCheckButton* button, gpointer user_data) {
    installer_window* self = static_cast<installer_window*>(user_data);
    if (gtk_check_button_get_active(button)) {
        self->m_data.use_download = false;
        self->update_iso_source_ui();
        self->update_navigation_state();
    }
}

void installer_window::on_download_folder_dialog_response(GObject* source_object,
                                                          GAsyncResult* result,
                                                          gpointer user_data) {
    installer_window* self = static_cast<installer_window*>(user_data);

    GtkFileDialog* dialog = GTK_FILE_DIALOG(source_object);
    GFile* folder = gtk_file_dialog_select_folder_finish(dialog, result, nullptr);

    if (folder) {
        char* folder_path = g_file_get_path(folder);
        if (folder_path) {
            gtk_label_set_text(GTK_LABEL(self->m_download_path_label), folder_path);
            self->m_data.download_path = folder_path;
            self->update_navigation_state();
        }
        g_free(folder_path);
        g_object_unref(folder);
    }
}

void installer_window::show_download_progress(bool show) {
    if (m_download_progress && GTK_IS_PROGRESS_BAR(m_download_progress)) {
        gtk_widget_set_visible(GTK_WIDGET(m_download_progress), show);
    }
    if (m_download_status_label && GTK_IS_LABEL(m_download_status_label)) {
        gtk_widget_set_visible(GTK_WIDGET(m_download_status_label), show);
    }
    if (m_loading_spinner && GTK_IS_SPINNER(m_loading_spinner)) {
        gtk_widget_set_visible(GTK_WIDGET(m_loading_spinner), !show);
    }
    if (m_banned_icon && GTK_IS_IMAGE(m_banned_icon)) {
        gtk_widget_set_visible(GTK_WIDGET(m_banned_icon), false);
    }
}

void installer_window::show_banned_state(bool show) {
    if (m_banned_icon && GTK_IS_IMAGE(m_banned_icon)) {
        gtk_widget_set_visible(GTK_WIDGET(m_banned_icon), show);
    }
    if (m_loading_spinner && GTK_IS_SPINNER(m_loading_spinner)) {
        gtk_widget_set_visible(GTK_WIDGET(m_loading_spinner), !show);
    }
    if (m_download_progress && GTK_IS_PROGRESS_BAR(m_download_progress)) {
        gtk_widget_set_visible(GTK_WIDGET(m_download_progress), false);
    }
    if (m_download_status_label && GTK_IS_LABEL(m_download_status_label)) {
        gtk_widget_set_visible(GTK_WIDGET(m_download_status_label), false);
    }
}

void installer_window::start_iso_download() {
    // Initialize Microsoft interface
    if (!m_microsoft_interface->initialize("en-US")) {
        if (m_loading_label && GTK_IS_LABEL(m_loading_label)) {
            gtk_label_set_text(m_loading_label, "Failed to initialize Microsoft interface");
        }
        update_navigation_state();
        return;
    }

    // Check if Microsoft interface is banned
    if (m_microsoft_interface->is_banned()) {
        show_banned_state(true);
        if (m_loading_label && GTK_IS_LABEL(m_loading_label)) {
            gtk_label_set_text(m_loading_label,
                               "Microsoft has temporarily blocked this IP address. "
                               "The restriction will likely be lifted in a few days. "
                               "Please try using an existing ISO file instead.");
        }

        update_navigation_state();
        return;
    }

    // Get SKU information for Windows 11
    auto skus =
        m_microsoft_interface->get_sku_by_edition(product_edition::redstone_consumer_x64_oem_dvd9);
    if (skus.empty()) {
        if (m_loading_label && GTK_IS_LABEL(m_loading_label)) {
            gtk_label_set_text(m_loading_label, "Failed to get Windows SKU information");
        }
        update_navigation_state();
        return;
    }

    // Get download URL for the first SKU
    std::string download_url = m_microsoft_interface->get_download_url(skus[0]);
    if (download_url.empty()) {
        if (m_loading_label && GTK_IS_LABEL(m_loading_label)) {
            gtk_label_set_text(m_loading_label, "Failed to get download URL");
        }
        update_navigation_state();
        return;
    }

    // Set up download options
    multipart_transfer::options opts;
    opts.max_threads = 4;
    opts.chunk_size_bytes = 4 * 1024 * 1024; // 4MB chunks
    opts.per_request_timeout_seconds = 60;

    // Set output file path
    std::string filename = skus[0].file_name;
    if (filename.empty()) {
        filename = "Windows11.iso";
    }
    m_data.iso_path = m_data.download_path + "/" + filename;
    opts.output_file_path = m_data.iso_path;

    // Start download
    m_downloader->download(
        download_url, opts,
        [this](const multipart_transfer::progress_info& info) { on_download_progress(info); },
        [this](bool success, const std::string& error) { on_download_complete(success, error); });
}

void installer_window::on_download_progress(const multipart_transfer::progress_info& info) {
    if (!m_window || !GTK_IS_WINDOW(m_window)) {
        return;
    }

    double progress = 0.0;
    if (info.global_total_bytes > 0) {
        progress = static_cast<double>(info.global_bytes_downloaded) /
                   static_cast<double>(info.global_total_bytes);
    }

    // Format speed
    std::string speed_str = "Unknown";
    if (info.global_bytes_per_sec > 0) {
        double mbps = info.global_bytes_per_sec / (1024.0 * 1024.0);
        speed_str = std::to_string(static_cast<int>(mbps)) + " MB/s";
    }

    // Format downloaded size
    double downloaded_mb = info.global_bytes_downloaded / (1024.0 * 1024.0);
    double total_mb = info.global_total_bytes / (1024.0 * 1024.0);

    std::string status = std::to_string(static_cast<int>(downloaded_mb)) + " MB / " +
                         std::to_string(static_cast<int>(total_mb)) + " MB (" + speed_str + ")";

    if (m_download_progress && GTK_IS_PROGRESS_BAR(m_download_progress)) {
        gtk_progress_bar_set_fraction(m_download_progress, progress);
    }
    if (m_download_status_label && GTK_IS_LABEL(m_download_status_label)) {
        gtk_label_set_text(m_download_status_label, status.c_str());
    }
}

void installer_window::on_download_complete(bool success, const std::string& error) {
    if (!m_window || !GTK_IS_WINDOW(m_window)) {
        return;
    }

    if (success) {
        // Download completed successfully, now scan the WIM
        show_download_progress(false);
        if (m_loading_label && GTK_IS_LABEL(m_loading_label)) {
            gtk_label_set_text(m_loading_label, "Scanning Windows ISO...");
        }

        // Start WIM scanning
        wim_scan_thread(this);
    } else {
        // Download failed
        show_download_progress(false);
        if (m_loading_label && GTK_IS_LABEL(m_loading_label)) {
            gtk_label_set_text(m_loading_label, ("Download failed: " + error).c_str());
        }

        // Update navigation state
        update_navigation_state();
    }
}

void installer_window::populate_windows_editions(const std::vector<std::string>& editions) {
    if (!m_windows_edition_combo || !ADW_IS_COMBO_ROW(m_windows_edition_combo)) {
        return;
    }

    // Create a new string list with the editions
    GtkStringList* string_list = gtk_string_list_new(nullptr);

    // Add the new editions
    for (const auto& edition : editions) {
        gtk_string_list_append(string_list, edition.c_str());
    }

    // Set the new model
    adw_combo_row_set_model(m_windows_edition_combo, G_LIST_MODEL(string_list));

    // Set the first item as selected
    if (!editions.empty()) {
        adw_combo_row_set_selected(m_windows_edition_combo, 0);
    }
}

std::string installer_window::get_selected_windows_edition() const {
    if (!m_windows_edition_combo || !ADW_IS_COMBO_ROW(m_windows_edition_combo)) {
        return ""; // Default fallback
    }

    guint selected = adw_combo_row_get_selected(m_windows_edition_combo);
    GtkStringList* string_list = GTK_STRING_LIST(adw_combo_row_get_model(m_windows_edition_combo));

    if (string_list && selected < g_list_model_get_n_items(G_LIST_MODEL(string_list))) {
        GtkStringObject* string_obj =
            GTK_STRING_OBJECT(g_list_model_get_item(G_LIST_MODEL(string_list), selected));
        if (string_obj) {
            return std::string(gtk_string_object_get_string(string_obj));
        }
    }

    return ""; // Default fallback
}

void installer_window::on_install_button_clicked(GtkButton* button, gpointer user_data) {
    installer_window* self = static_cast<installer_window*>(user_data);
    self->start_vm_installation();
}

void installer_window::start_vm_installation() {
    // Collect VM settings from UI
    collect_vm_settings();

    // Disable the install button to prevent multiple clicks
    gtk_widget_set_sensitive(GTK_WIDGET(m_install_button), false);

    // Start the VM installation process
    nlohmann::json params = {{"vm_name", "LSWVM"},
                             {"iso_path", m_data.iso_path},
                             {"windows_edition", get_selected_windows_edition()},
                             {"admin_username", m_data.admin_username},
                             {"admin_password", m_data.admin_password},
                             {"memory_gb", m_data.memory_gb},
                             {"cpu_cores", m_data.cpu_cores},
                             {"disk_gb", m_data.disk_gb},
                             {"hardware_acceleration", m_data.hardware_acceleration}};

    application::instance().get_ipc().execute_workload(
        workload_type::install_vm, params,
        [this](const nlohmann::json& result) {
            // VM installation completed successfully
            if (!m_window || !GTK_IS_WINDOW(m_window)) {
                return;
            }

            append_progress_message("VM installation completed successfully!");
            append_progress_message("VM Name: " + result.value("vm_name", "Unknown"));
            append_progress_message("Status: " + result.value("status", "Unknown"));

            // Re-enable the install button
            gtk_widget_set_sensitive(GTK_WIDGET(m_install_button), true);
        },
        [this](const std::string& error) {
            // VM installation failed
            if (!m_window || !GTK_IS_WINDOW(m_window)) {
                return;
            }

            append_progress_message("VM installation failed: " + error);

            // Re-enable the install button
            gtk_widget_set_sensitive(GTK_WIDGET(m_install_button), true);
        },
        [this](const std::string& progress) {
            // VM installation progress update
            if (!m_window || !GTK_IS_WINDOW(m_window)) {
                return;
            }

            append_progress_message(progress);
        });
}

void installer_window::collect_vm_settings() {
    // Collect memory setting
    if (m_memory_spinner && ADW_IS_SPIN_ROW(m_memory_spinner)) {
        m_data.memory_gb = adw_spin_row_get_value(m_memory_spinner);
    }

    // Collect storage setting
    if (m_storage_spinner && ADW_IS_SPIN_ROW(m_storage_spinner)) {
        m_data.disk_gb = adw_spin_row_get_value(m_storage_spinner);
    }

    // Collect CPU setting
    if (m_cpu_spinner && ADW_IS_SPIN_ROW(m_cpu_spinner)) {
        m_data.cpu_cores = adw_spin_row_get_value(m_cpu_spinner);
    }

    // Collect admin username
    if (m_admin_username_entry && ADW_IS_ENTRY_ROW(m_admin_username_entry)) {
        const char* username = gtk_editable_get_text(GTK_EDITABLE(m_admin_username_entry));
        if (username) {
            m_data.admin_username = username;
        }
    }

    // Collect admin password
    if (m_admin_password_entry && ADW_IS_PASSWORD_ENTRY_ROW(m_admin_password_entry)) {
        const char* password = gtk_editable_get_text(GTK_EDITABLE(m_admin_password_entry));
        if (password) {
            m_data.admin_password = password;
        }
    }

    // Collect hardware acceleration setting
    if (m_hardware_accel_check && GTK_IS_CHECK_BUTTON(m_hardware_accel_check)) {
        m_data.hardware_acceleration = gtk_check_button_get_active(m_hardware_accel_check);
    }
}
