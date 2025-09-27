#pragma once

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Logging control: define HTTP_CLIENT_ENABLE_LOG to enable client logs
#ifdef HTTP_CLIENT_ENABLE_LOG
#include <iostream>
#define HTTP_CLIENT_LOG(stmt)                                                                      \
    do {                                                                                           \
        stmt;                                                                                      \
    } while (0)
#else
#define HTTP_CLIENT_LOG(stmt)                                                                      \
    do {                                                                                           \
    } while (0)
#endif

class http_client {
public:
    struct response {
        int status_code;
        std::string body;
        std::map<std::string, std::string> headers;

        response() : status_code(0) {}
    };

    struct request {
        std::string url;
        std::map<std::string, std::string> headers;
        std::string body;

        request(const std::string& url) : url(url) {}
    };

    http_client();
    ~http_client();

    // Disable copy constructor and assignment operator
    http_client(const http_client&) = delete;
    http_client& operator=(const http_client&) = delete;

    // Enable move constructor and assignment operator
    http_client(http_client&&) noexcept;
    http_client& operator=(http_client&&) noexcept;

    // HTTP methods
    response get(const std::string& url);
    response get(const request& req);
    response post(const std::string& url, const std::string& data = "");
    response post(const request& req);

    // Session management
    void set_timeout(long timeout_seconds);

    // Cookie management (via libcurl cookie engine)
    void set_cookie(const std::string& name, const std::string& value);
    void set_cookie(const std::string& name, const std::string& value, const std::string& domain,
                    const std::string& path = "/");
    std::string get_cookie(const std::string& name) const;
    void clear_cookies();
    void print_all_cookies() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl;

    response perform_request(const request& req, bool is_post);
    void setup_curl_handle(void* curl_handle);
    void parse_response_headers(const std::string& header_string, response& resp);
    void print_request_details(const request& req, bool is_post);
    void print_response_details(const response& resp);
};
