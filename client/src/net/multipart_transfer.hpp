#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/http.hpp"

class multipart_transfer {
public:
    struct progress_info {
        std::size_t part_index;
        std::uint64_t part_bytes_downloaded;
        std::uint64_t part_total_bytes;
        std::uint64_t global_bytes_downloaded;
        std::uint64_t global_total_bytes;
        double part_bytes_per_sec;
        double global_bytes_per_sec;
    };

    using progress_callback_t = std::function<void(const progress_info&)>;
    using completion_callback_t =
        std::function<void(bool success, const std::string& error_message)>;

    struct options {
        std::size_t max_threads;          // number of concurrent downloads (also part count)
        std::uint64_t chunk_size_bytes;   // sub-request chunk size within each part
        long per_request_timeout_seconds; // curl timeout per request
        std::string output_file_path;     // path to write the downloaded file

        options();
    };

    multipart_transfer() = default;

    // Downloads the resource at `url` using HTTP Range requests.
    // If output_file_path is provided in options, writes directly to file.
    // Otherwise, downloads into memory.
    // Calls `on_progress` as bytes are written across all parts.
    // Calls `on_complete` once with success=false on first fatal error, or success=true when done.
    void download(const std::string& url, const options& opts,
                  const progress_callback_t& on_progress, const completion_callback_t& on_complete);

    // Request cooperative cancellation. Safe to call from callbacks/other threads.
    void cancel();

    // Access the aggregated downloaded bytes after successful completion.
    const std::vector<std::uint8_t>& data() const {
        return buffer_;
    }

private:
    struct part_range {
        std::uint64_t start;
        std::uint64_t end_inclusive; // HTTP Range end is inclusive
    };

    static bool server_supports_ranges(const http_client::response& head_like_response);
    static std::uint64_t parse_content_length(const http_client::response& response);

    std::vector<part_range> plan_parts(std::uint64_t total_bytes, const options& opts) const;
    bool download_part(const std::string& url, const part_range& range, std::size_t part_index,
                       std::vector<std::uint8_t>& target,
                       std::atomic<std::uint64_t>& global_written,
                       const progress_callback_t& on_progress, std::string& out_error,
                       const std::chrono::steady_clock::time_point& global_start_tp,
                       const options& opts);

private:
    std::atomic<bool> cancel_requested_;
    std::vector<std::uint8_t> buffer_;
};
