#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "net/http.hpp"

enum class product_edition { redstone_consumer_x64_oem_dvd9 = 3113 };

struct sku_info {
    std::string id;
    std::string product_name;
    std::string file_name;
    std::string language;
};

class microsoft_interface {
public:
    microsoft_interface() = default;
    ~microsoft_interface() = default;

    bool initialize(const std::string& locale);
    std::vector<sku_info> get_sku_by_edition(product_edition edition);
    std::string get_download_url(const sku_info& sku);
    bool is_banned();

private:
    http_client m_http;
    std::string m_locale;
    std::string m_session_id;
    bool m_is_banned = false;
    bool check_locale();
    std::string generate_session_id();
    nlohmann::json parse_microsoft_response(const std::string& response_body);
    void visit_download_page();
    bool whitelist_session(const std::string& session_id);
};