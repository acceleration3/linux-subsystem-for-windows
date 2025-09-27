#include "net/http.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <curl/curl.h>

constexpr const char* USER_AGENT =
    "Mozilla/5.0 (X11; Linux x86_64; rv:143.0) Gecko/20100101 Firefox/143.0";
namespace {
// (no shared CURL caches)
// Callback function to write response data
size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Callback function to write response headers
size_t header_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Helper function to convert string to lowercase
std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Helper function to trim whitespace
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

// Helper function to extract domain from URL
std::string extract_domain_from_url(const std::string& url) {
    // Find the protocol separator
    size_t protocol_pos = url.find("://");
    if (protocol_pos == std::string::npos) {
        return "";
    }

    // Find the start of the domain
    size_t domain_start = protocol_pos + 3;

    // Find the end of the domain (before path or port)
    size_t domain_end = url.find('/', domain_start);
    if (domain_end == std::string::npos) {
        domain_end = url.find(':', domain_start);
    }
    if (domain_end == std::string::npos) {
        domain_end = url.length();
    }

    return url.substr(domain_start, domain_end - domain_start);
}
} // namespace

class http_client::impl {
public:
    impl() : curl_handle(nullptr), user_agent("HTTP Client/1.0"), timeout_seconds(30) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_handle = curl_easy_init();
        if (!curl_handle) {
            throw std::runtime_error("Failed to initialize curl handle");
        }

        // Set up common curl options
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 0L);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, timeout_seconds);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, USER_AGENT);
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);
        // Prefer HTTP/2 over TLS if available (falls back automatically)
        curl_easy_setopt(curl_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
#ifdef HTTP_CLIENT_ENABLE_LOG
        // Emit full request/response details from libcurl when logging is enabled
        // curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);
#endif

        // Enable automatic decompression
        curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

        // Enable cookie engine and writeback
        curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
        curl_easy_setopt(curl_handle, CURLOPT_COOKIEJAR, "");
    }

    ~impl() {
        if (curl_handle) {
            curl_easy_cleanup(curl_handle);
        }
        curl_global_cleanup();
    }

    // Disable copy constructor and assignment operator
    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    // Enable move constructor and assignment operator
    impl(impl&& other) noexcept
        : curl_handle(other.curl_handle), user_agent(std::move(other.user_agent)),
          timeout_seconds(other.timeout_seconds) {
        other.curl_handle = nullptr;
    }

    impl& operator=(impl&& other) noexcept {
        if (this != &other) {
            if (curl_handle) {
                curl_easy_cleanup(curl_handle);
            }
            curl_handle = other.curl_handle;
            user_agent = std::move(other.user_agent);
            timeout_seconds = other.timeout_seconds;
            other.curl_handle = nullptr;
        }
        return *this;
    }

    CURL* curl_handle;
    std::string user_agent;
    long timeout_seconds;
    std::string cookie_header;
};

http_client::http_client() : pimpl(std::make_unique<impl>()) {}

http_client::~http_client() = default;

http_client::http_client(http_client&&) noexcept = default;
http_client& http_client::operator=(http_client&&) noexcept = default;

http_client::response http_client::get(const std::string& url) {
    request req(url);
    return get(req);
}

http_client::response http_client::get(const request& req) {
    return perform_request(req, false);
}

http_client::response http_client::post(const std::string& url, const std::string& data) {
    request req(url);
    req.body = data;
    return post(req);
}

http_client::response http_client::post(const request& req) {
    return perform_request(req, true);
}

void http_client::set_timeout(long timeout_seconds) {
    pimpl->timeout_seconds = timeout_seconds;
    curl_easy_setopt(pimpl->curl_handle, CURLOPT_TIMEOUT, timeout_seconds);
}

http_client::response http_client::perform_request(const request& req, bool is_post) {
    response resp;
    std::string response_body;
    std::string response_headers;

    // Print request details
    print_request_details(req, is_post);

    // Set URL
    curl_easy_setopt(pimpl->curl_handle, CURLOPT_URL, req.url.c_str());

    // Set write callbacks
    curl_easy_setopt(pimpl->curl_handle, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(pimpl->curl_handle, CURLOPT_HEADERDATA, &response_headers);

    // Set HTTP method
    if (is_post) {
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_POSTFIELDSIZE, req.body.length());
    } else {
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_POST, 0L);
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_UPLOAD, 0L);
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_CUSTOMREQUEST, "GET");
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_POSTFIELDS, nullptr);
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_POSTFIELDSIZE, 0L);
    }

    // Set custom headers
    struct curl_slist* header_list = nullptr;
    for (const auto& header : req.headers) {
        std::string header_string = header.first + ": " + header.second;
        header_list = curl_slist_append(header_list, header_string.c_str());
    }
    if (header_list) {
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_HTTPHEADER, header_list);
    }

    // Perform the request
    CURLcode res = curl_easy_perform(pimpl->curl_handle);

    // Clean up headers and clear from handle to avoid dangling pointer across requests
    if (header_list) {
        curl_easy_setopt(pimpl->curl_handle, CURLOPT_HTTPHEADER, nullptr);
        curl_slist_free_all(header_list);
        header_list = nullptr;
    }

    if (res != CURLE_OK) {
        HTTP_CLIENT_LOG(std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res)
                                  << std::endl);
        return resp;
    }

    // Get status code
    long status_code;
    curl_easy_getinfo(pimpl->curl_handle, CURLINFO_RESPONSE_CODE, &status_code);
    resp.status_code = static_cast<int>(status_code);

    // Log effective URL after redirects
    char* effective_url = nullptr;
    if (curl_easy_getinfo(pimpl->curl_handle, CURLINFO_EFFECTIVE_URL, &effective_url) == CURLE_OK &&
        effective_url) {
        HTTP_CLIENT_LOG(std::cout << "Effective URL: " << effective_url << std::endl);
    }

    // Parse response headers
    parse_response_headers(response_headers, resp);

    // Set response body
    resp.body = response_body;

    // Print response details
    print_response_details(resp);

    return resp;
}

void http_client::parse_response_headers(const std::string& header_string, response& resp) {
    std::istringstream stream(header_string);
    std::string line;

    while (std::getline(stream, line)) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string header_name = trim(line.substr(0, colon_pos));
            std::string header_value = trim(line.substr(colon_pos + 1));

            // Convert header name to lowercase for case-insensitive comparison
            std::string lower_header_name = to_lower(header_name);
            resp.headers[lower_header_name] = header_value;
        }
    }
}

void http_client::print_request_details(const request& req, bool is_post) {
    HTTP_CLIENT_LOG(std::cout << "\n=== HTTP REQUEST ===" << std::endl);
    HTTP_CLIENT_LOG(std::cout << "Method: " << (is_post ? "POST" : "GET") << std::endl);
    HTTP_CLIENT_LOG(std::cout << "URL: " << req.url << std::endl);

    if (!req.headers.empty()) {
        HTTP_CLIENT_LOG(std::cout << "Headers:" << std::endl);
        for (const auto& header : req.headers) {
            HTTP_CLIENT_LOG(std::cout << "  " << header.first << ": " << header.second
                                      << std::endl);
        }
    }
    HTTP_CLIENT_LOG(std::cout << "===================\n" << std::endl);
}

void http_client::print_response_details(const response& resp) {
    HTTP_CLIENT_LOG(std::cout << "\n=== HTTP RESPONSE ===" << std::endl);
    HTTP_CLIENT_LOG(std::cout << "Status Code: " << resp.status_code << std::endl);

    if (!resp.headers.empty()) {
        HTTP_CLIENT_LOG(std::cout << "Headers:" << std::endl);
        for (const auto& header : resp.headers) {
            HTTP_CLIENT_LOG(std::cout << "  " << header.first << ": " << header.second
                                      << std::endl);
        }
    }
    // Print cookies from libcurl's cookie engine
    print_all_cookies();
    HTTP_CLIENT_LOG(std::cout << "====================\n" << std::endl);
}

// Cookie management helpers
void http_client::set_cookie(const std::string& name, const std::string& value) {
    // Best-effort simple cookie; libcurl will associate it with the last used host
    std::string cookie_line = name + "=" + value;
    curl_easy_setopt(pimpl->curl_handle, CURLOPT_COOKIELIST, cookie_line.c_str());
}

void http_client::set_cookie(const std::string& name, const std::string& value,
                             const std::string& domain, const std::string& path) {
    // Netscape cookie file format: domain \t include_subdomains \t path \t secure \t expires \t
    // name \t value include_subdomains=TRUE, secure=FALSE, expires=0 (session cookie)
    std::string netscape_cookie = domain + "\tTRUE\t" + path + "\tFALSE\t0\t" + name + "\t" + value;
    curl_easy_setopt(pimpl->curl_handle, CURLOPT_COOKIELIST, netscape_cookie.c_str());
}

std::string http_client::get_cookie(const std::string& name) const {
    struct curl_slist* cookies = nullptr;
    if (curl_easy_getinfo(pimpl->curl_handle, CURLINFO_COOKIELIST, &cookies) != CURLE_OK) {
        return "";
    }

    std::string found_value;
    for (struct curl_slist* nc = cookies; nc; nc = nc->next) {
        // Each line is Netscape cookie format with tabs
        std::string line = nc->data ? nc->data : "";
        // Find name and value at the end (fields 6 and 7)
        // We split from the end to avoid parsing domain/path intricacies
        size_t last_tab = line.rfind('\t');
        if (last_tab == std::string::npos)
            continue;
        std::string value_field = line.substr(last_tab + 1);
        std::string rest = line.substr(0, last_tab);
        size_t name_tab = rest.rfind('\t');
        if (name_tab == std::string::npos)
            continue;
        std::string name_field = rest.substr(name_tab + 1);
        if (name_field == name) {
            found_value = value_field;
            break;
        }
    }
    if (cookies) {
        curl_slist_free_all(cookies);
    }
    return found_value;
}

void http_client::clear_cookies() {
    // Clear all cookies
    curl_easy_setopt(pimpl->curl_handle, CURLOPT_COOKIELIST, "ALL");
}

void http_client::print_all_cookies() const {
    struct curl_slist* cookies = nullptr;
    CURLcode rc = curl_easy_getinfo(pimpl->curl_handle, CURLINFO_COOKIELIST, &cookies);
    if (rc != CURLE_OK || !cookies) {
        HTTP_CLIENT_LOG(std::cout << "No cookies." << std::endl);
        return;
    }
    HTTP_CLIENT_LOG(std::cout << "Cookies:" << std::endl);
    for (struct curl_slist* nc = cookies; nc; nc = nc->next) {
        HTTP_CLIENT_LOG(std::cout << "  " << (nc->data ? nc->data : "") << std::endl);
    }
    curl_slist_free_all(cookies);
}
