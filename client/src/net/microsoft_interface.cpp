#include "net/microsoft_interface.hpp"

#include <iostream>
#include <random>
#include <sstream>

#include <cctype>
#include <cstdint>

#include <nlohmann/json.hpp>

constexpr const char* ORG_ID = "y6jn8c31";
constexpr const char* PROFILE = "606624d44113";
constexpr const char* USER_AGENT =
    "Mozilla/5.0 (X11; Linux x86_64; rv:143.0) Gecko/20100101 Firefox/143.0";

namespace {
std::string normalize_whitespace(const std::string& input) {
    // Replace UTF-8 NBSP (\xC2\xA0) with regular space, then
    // collapse runs of whitespace to a single space and trim.
    std::string tmp;
    tmp.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == 0xC2 && i + 1 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xA0) {
            tmp.push_back(' ');
            ++i; // skip second byte of NBSP
        } else {
            tmp.push_back(static_cast<char>(c));
        }
    }

    std::string out;
    out.reserve(tmp.size());
    bool in_space = false;
    for (unsigned char ch : tmp) {
        if (std::isspace(ch)) {
            if (!in_space) {
                out.push_back(' ');
                in_space = true;
            }
        } else {
            out.push_back(static_cast<char>(ch));
            in_space = false;
        }
    }
    // trim
    size_t start = 0;
    while (start < out.size() && out[start] == ' ')
        ++start;
    size_t end = out.size();
    while (end > start && out[end - 1] == ' ')
        --end;
    return out.substr(start, end - start);
}
} // namespace

bool microsoft_interface::initialize(const std::string& locale) {
    m_locale = locale;

    // Check locale and fall back to en-US if needed
    if (!check_locale()) {
        std::cerr << "Failed to validate locale" << std::endl;
        return false;
    }

    m_session_id = generate_session_id();
    std::cout << "Session ID: " << m_session_id << std::endl;

    // Visit the download page to get the cookie header
    visit_download_page();

    // Whitelist the session
    if (!whitelist_session(m_session_id)) {
        std::cerr << "Failed to whitelist session" << std::endl;
        return false;
    }

    return true;
}

std::string microsoft_interface::generate_session_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    auto hex = [&](uint32_t v, int w) {
        std::stringstream ss;
        ss << std::hex << std::nouppercase << std::setfill('0') << std::setw(w) << v;
        return ss.str();
    };
    uint32_t d1 = dist(gen);
    uint16_t d2 = dist(gen) & 0xFFFF;
    uint16_t d3 = (dist(gen) & 0x0FFF) | 0x4000; // version 4
    uint16_t d4 = (dist(gen) & 0x3FFF) | 0x8000; // variant
    uint16_t d5 = dist(gen) & 0xFFFF;
    uint32_t d6 = dist(gen);
    std::stringstream ss;
    ss << hex(d1, 8) << "-" << hex(d2, 4) << "-" << hex(d3, 4) << "-" << hex(d4, 4) << "-"
       << hex(d5, 4) << hex(d6, 8);
    return ss.str();
}

void microsoft_interface::visit_download_page() {
    http_client::request req("https://www.microsoft.com/software-download/windows11");
    req.headers["User-Agent"] = USER_AGENT;
    req.headers["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
    req.headers["Accept-Language"] = "en-US,en;q=0.5";
    req.headers["Accept-Encoding"] = "gzip, deflate, br, zstd";
    req.headers["Connection"] = "keep-alive";
    auto response = m_http.get(req);
}

bool microsoft_interface::whitelist_session(const std::string& session_id) {

    // Build the URL with query parameters
    std::string url = "https://vlscppe.microsoft.com/tags?org_id=" + std::string(ORG_ID) +
                      "&session_id=" + session_id;

    // Create request object
    http_client::request req(url);
    req.headers["User-Agent"] = USER_AGENT;
    req.headers["Accept"] = "*/*";
    req.headers["Accept-Language"] = "en-US,en;q=0.5";
    req.headers["Accept-Encoding"] = "gzip, deflate, br, zstd";
    req.headers["Referer"] = "https://www.microsoft.com/software-download/windows11";
    req.headers["Connection"] = "keep-alive";
    // Send the GET request
    auto response = m_http.get(req);

    // Check if the request was successful
    if (response.status_code == 200) {
        std::cout << "Session whitelisted successfully" << std::endl;
        return true;
    } else {
        std::cerr << "Failed to whitelist session. Status code: " << response.status_code
                  << std::endl;
        return false;
    }
}

std::string microsoft_interface::get_download_url(const sku_info& sku) {
    std::string url = "https://www.microsoft.com/software-download-connector/api/"
                      "GetProductDownloadLinksBySku?profile=" +
                      std::string(PROFILE) + "&ProductEditionId=undefined&SKU=" + sku.id +
                      "&friendlyFileName=undefined&Locale=" + m_locale +
                      "&sessionID=" + m_session_id;
    http_client::request req(url);
    req.headers["User-Agent"] = USER_AGENT;
    req.headers["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
    req.headers["Accept-Language"] = "en-US,en;q=0.5";
    req.headers["Accept-Encoding"] = "gzip, deflate, br, zstd";
    req.headers["Connection"] = "keep-alive";
    req.headers["Referer"] = "https://www.microsoft.com/software-download/windows11";
    auto response = m_http.get(req);

    auto json = parse_microsoft_response(response.body);
    if (json.empty()) {
        std::cerr << "Failed to get download links" << std::endl;
        return {};
    }

    if (json["ProductDownloadOptions"].empty()) {
        std::cerr << "No product download options found" << std::endl;
        return {};
    }
    return json["ProductDownloadOptions"][0]["Uri"];
}

std::vector<sku_info> microsoft_interface::get_sku_by_edition(product_edition edition) {
    std::string url = "https://www.microsoft.com/software-download-connector/api/"
                      "getskuinformationbyproductedition?profile=" +
                      std::string(PROFILE) +
                      "&ProductEditionId=" + std::to_string(static_cast<int>(edition)) +
                      "&SKU=undefined&friendlyFileName=undefined&Locale=" + m_locale +
                      "&sessionID=" + m_session_id;
    http_client::request req(url);
    req.headers["User-Agent"] = USER_AGENT;
    req.headers["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
    req.headers["Accept-Language"] = "en-US,en;q=0.5";
    req.headers["Accept-Encoding"] = "gzip, deflate, br, zstd";
    req.headers["Connection"] = "keep-alive";
    auto response = m_http.get(req);

    std::vector<sku_info> skus;
    auto json = parse_microsoft_response(response.body);
    if (json.empty()) {
        std::cerr << "Failed to get SKU information" << std::endl;
        return skus;
    }

    auto skus_json = json["Skus"];
    for (auto& sku : skus_json) {
        sku_info info;
        info.id = sku["Id"];
        info.product_name = normalize_whitespace(sku["LocalizedProductDisplayName"]);
        info.file_name = sku["FriendlyFileNames"][0];
        info.language = sku["Language"];
        skus.push_back(info);
    }

    return skus;
}

bool microsoft_interface::is_banned() {
    return m_is_banned;
}

nlohmann::json microsoft_interface::parse_microsoft_response(const std::string& response_body) {
    try {
        nlohmann::json json = nlohmann::json::parse(response_body);

        // Check for Microsoft-specific errors
        if (json.contains("Errors") && !json["Errors"].empty()) {
            auto error = json["Errors"][0];
            if (error.contains("Type") && error["Type"] == 9) {
                std::cerr
                    << "Microsoft error 715-123130: IP address may be banned or region restricted"
                    << std::endl;
                std::cerr << "Session ID: " << m_session_id << std::endl;
                m_is_banned = true;              // Only this specific error indicates a ban
                return nlohmann::json::object(); // Return empty object to indicate error
            } else if (error.contains("Value")) {
                std::cerr << "Microsoft API error: " << error["Value"] << std::endl;
                return nlohmann::json::object(); // Return empty object to indicate error
            }
        }

        return json;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse response: " << e.what() << std::endl;
        return nlohmann::json::object(); // Return empty object to indicate error
    }
}

bool microsoft_interface::check_locale() {
    // Check if the locale we want is available - fall back to en-US otherwise
    std::string url = "https://www.microsoft.com/" + m_locale + "/software-download/";

    http_client::request req(url);
    req.headers["User-Agent"] = USER_AGENT;
    req.headers["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
    req.headers["Accept-Language"] = "en-US,en;q=0.5";
    req.headers["Accept-Encoding"] = "gzip, deflate, br, zstd";
    req.headers["Connection"] = "keep-alive";

    try {
        auto response = m_http.get(req);
        if (response.status_code == 200) {
            std::cout << "Locale check successful for: " << m_locale << std::endl;
            return true;
        } else {
            std::cerr << "Locale check failed for: " << m_locale
                      << " (status: " << response.status_code << ")" << std::endl;
            // Fall back to en-US
            if (m_locale != "en-US") {
                std::cout << "Falling back to en-US locale" << std::endl;
                m_locale = "en-US";
                return true;
            }
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Locale check failed with exception: " << e.what() << std::endl;
        // Fall back to en-US
        if (m_locale != "en-US") {
            std::cout << "Falling back to en-US locale" << std::endl;
            m_locale = "en-US";
            return true;
        }
        return false;
    }
}
