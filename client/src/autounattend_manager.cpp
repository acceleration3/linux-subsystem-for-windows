#include <fstream>
#include "autounattend_manager.hpp"
#include "templates/autounattend_template.hpp"

std::string autounattend_manager::generate_autounattend(const configuration& config) const {
    // Get the template content
    std::string template_content = autounattend::template_xml;

    // Replace placeholders with actual values
    return replace_placeholders(template_content, config);
}

bool autounattend_manager::generate_autounattend_file(const configuration& config,
                                                      const std::string& output_path) const {
    try {
        std::string content = generate_autounattend(config);

        std::ofstream file(output_path);
        if (!file.is_open()) {
            return false;
        }

        file << content;
        file.close();

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool autounattend_manager::validate_configuration(const configuration& config) const {
    // Validate product key format (basic check for Windows 11 Pro key format)
    if (config.product_key.length() != 29 || config.product_key.find('-') == std::string::npos) {
        return false;
    }

    // Validate Windows edition index (should be between 1-10 for most Windows editions)
    if (config.windows_edition_index < 1 || config.windows_edition_index > 10) {
        return false;
    }

    // Validate computer name (should not be empty and follow Windows naming conventions)
    if (config.computer_name.empty() || config.computer_name.length() > 15) {
        return false;
    }

    // Validate username (should not be empty)
    if (config.username.empty()) {
        return false;
    }

    // Validate display name (should not be empty)
    if (config.display_name.empty()) {
        return false;
    }

    // Validate password (should not be empty)
    if (config.password.empty()) {
        return false;
    }

    return true;
}

std::string autounattend_manager::replace_placeholders(const std::string& template_content,
                                                       const configuration& config) const {
    std::string result = template_content;

    // Replace placeholders using simple string replacement
    // Product key
    size_t pos = result.find("PRODUCT_KEY_PLACEHOLDER");
    if (pos != std::string::npos) {
        result.replace(pos, 23, config.product_key);
    }

    // Windows edition index
    pos = result.find("WINDOWS_EDITION_INDEX_PLACEHOLDER");
    if (pos != std::string::npos) {
        result.replace(pos, 33, std::to_string(config.windows_edition_index));
    }

    // Computer name
    pos = result.find("VM_NAME_PLACEHOLDER");
    if (pos != std::string::npos) {
        result.replace(pos, 46, config.computer_name);
    }

    // Username (multiple occurrences)
    pos = 0;
    while ((pos = result.find("LSW_USER_NAME", pos)) != std::string::npos) {
        result.replace(pos, 13, config.username);
        pos += config.username.length();
    }

    // Display name
    pos = 0;
    while ((pos = result.find("LSW_DISPLAY_NAME", pos)) != std::string::npos) {
        result.replace(pos, 16, config.display_name);
        pos += config.display_name.length();
    }

    // Password (multiple occurrences)
    pos = 0;
    while ((pos = result.find("LSW_USER_PASS", pos)) != std::string::npos) {
        result.replace(pos, 13, config.password);
        pos += config.password.length();
    }

    return result;
}