#pragma once

#include <string>

class autounattend_manager {
public:
    struct configuration {
        std::string product_key;
        int windows_edition_index;
        std::string computer_name;
        std::string username;
        std::string display_name;
        std::string password;
    };

    autounattend_manager() = default;
    ~autounattend_manager() = default;

    // Generate autounattend.xml content with custom parameters
    std::string generate_autounattend(const configuration& config) const;

    // Generate autounattend.xml and save to file
    bool generate_autounattend_file(const configuration& config,
                                    const std::string& output_path) const;

    // Validate configuration parameters
    bool validate_configuration(const configuration& config) const;

private:
    // Replace placeholders in template with actual values
    std::string replace_placeholders(const std::string& template_content,
                                     const configuration& config) const;
};