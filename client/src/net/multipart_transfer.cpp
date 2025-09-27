#include "net/multipart_transfer.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>

namespace {
inline std::string to_lower_copy(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}
} // namespace

multipart_transfer::options::options()
    : max_threads(8), chunk_size_bytes(4ULL * 1024ULL * 1024ULL), per_request_timeout_seconds(60),
      output_file_path("") {}

void multipart_transfer::cancel() {
    cancel_requested_.store(true, std::memory_order_relaxed);
}

bool multipart_transfer::server_supports_ranges(const http_client::response& resp) {
    // If we received a 206 Partial Content or a Content-Range header, ranges are supported
    if (resp.status_code == 206)
        return true;
    auto it_cr = resp.headers.find("content-range");
    if (it_cr != resp.headers.end())
        return true;
    // Otherwise, rely on Accept-Ranges if provided
    auto it = resp.headers.find("accept-ranges");
    if (it == resp.headers.end())
        return false;
    std::string v = to_lower_copy(it->second);
    return v.find("bytes") != std::string::npos;
}

std::uint64_t multipart_transfer::parse_content_length(const http_client::response& resp) {
    // Prefer Content-Range total size if present (e.g., "bytes 0-0/2398523392")
    auto it_cr = resp.headers.find("content-range");
    if (it_cr != resp.headers.end()) {
        const std::string& v = it_cr->second;
        auto slash_pos = v.rfind('/');
        if (slash_pos != std::string::npos && slash_pos + 1 < v.size()) {
            std::string total_str = v.substr(slash_pos + 1);
            try {
                auto total = static_cast<std::uint64_t>(std::stoull(total_str));
                if (total > 0)
                    return total;
            } catch (const std::exception& e) {
                std::cerr << "Error parsing content-range total: " << e.what() << std::endl;
            }
        }
    }

    // Fallback to Content-Length (works for non-range full responses)
    auto it = resp.headers.find("content-length");
    if (it == resp.headers.end())
        return 0;
    try {
        return static_cast<std::uint64_t>(std::stoull(it->second));
    } catch (const std::exception& e) {
        std::cerr << "Error parsing content length: " << e.what() << std::endl;
        return 0;
    }
}

std::vector<multipart_transfer::part_range> multipart_transfer::plan_parts(
    std::uint64_t total_bytes, const options& opts) const {
    std::vector<part_range> parts;
    if (total_bytes == 0)
        return parts;

    std::size_t parts_desired = opts.max_threads > 0 ? opts.max_threads : 1;
    if (parts_desired > total_bytes)
        parts_desired = static_cast<std::size_t>(total_bytes);

    std::uint64_t base = total_bytes / parts_desired;
    std::uint64_t rem = total_bytes % parts_desired;

    std::uint64_t offset = 0;
    for (std::size_t i = 0; i < parts_desired; ++i) {
        std::uint64_t size = base + (i < rem ? 1 : 0);
        std::uint64_t start = offset;
        std::uint64_t end = start + size - 1;
        parts.push_back({start, end});
        offset += size;
    }
    return parts;
}

bool multipart_transfer::download_part(const std::string& url, const part_range& range,
                                       std::size_t part_index, std::vector<std::uint8_t>& target,
                                       std::atomic<std::uint64_t>& global_written,
                                       const progress_callback_t& on_progress,
                                       std::string& out_error,
                                       const std::chrono::steady_clock::time_point& global_start_tp,
                                       const options& opts) {
    if (cancel_requested_.load(std::memory_order_relaxed)) {
        out_error = "cancelled";
        return false;
    }

    http_client client;
    client.set_timeout(opts.per_request_timeout_seconds);

    std::uint64_t part_total = range.end_inclusive - range.start + 1;
    std::uint64_t part_done = 0;
    auto part_start_tp = std::chrono::steady_clock::now();

    while (part_done < part_total) {
        if (cancel_requested_.load(std::memory_order_relaxed)) {
            out_error = "cancelled";
            return false;
        }

        std::uint64_t chunk =
            std::min<std::uint64_t>(opts.chunk_size_bytes, part_total - part_done);
        std::uint64_t chunk_start = range.start + part_done;
        std::uint64_t chunk_end = chunk_start + chunk - 1;

        http_client::request req(url);
        req.headers["Range"] =
            "bytes=" + std::to_string(chunk_start) + "-" + std::to_string(chunk_end);

        auto resp = client.get(req);
        if (resp.status_code != 206 && resp.status_code != 200) {
            out_error = "http status " + std::to_string(resp.status_code);
            return false;
        }

        const std::string& body = resp.body;
        if (resp.status_code == 200) {
            // Server ignored range; ensure we can copy the desired window
            if (body.size() < (chunk_end + 1)) {
                out_error = "unexpected short 200 body";
                return false;
            }
            std::memcpy(&target[chunk_start],
                        body.data() + static_cast<std::ptrdiff_t>(chunk_start),
                        static_cast<size_t>(chunk));
        } else {
            if (body.size() != chunk) {
                out_error = "partial body length mismatch";
                return false;
            }
            std::memcpy(&target[chunk_start], body.data(), static_cast<size_t>(chunk));
        }

        part_done += chunk;
        auto written = global_written.fetch_add(chunk, std::memory_order_relaxed) + chunk;
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        double part_secs =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - part_start_tp).count();
        double global_secs =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - global_start_tp)
                .count();
        double part_bps = part_secs > 0.0 ? static_cast<double>(part_done) / part_secs : 0.0;
        double global_bps = global_secs > 0.0 ? static_cast<double>(written) / global_secs : 0.0;
        if (on_progress) {
            progress_info p{part_index,    part_done, part_total, written,
                            target.size(), part_bps,  global_bps};
            on_progress(p);
        }
    }

    return true;
}

void multipart_transfer::download(const std::string& url, const options& opts,
                                  const progress_callback_t& on_progress,
                                  const completion_callback_t& on_complete) {
    cancel_requested_.store(false, std::memory_order_relaxed);
    std::cout << "[multipart_transfer] Starting download: " << url << std::endl;
    std::cout << "[multipart_transfer] max_threads(parts)=" << opts.max_threads
              << ", timeout_s=" << opts.per_request_timeout_seconds << std::endl;

    if (!opts.output_file_path.empty()) {
        std::cout << "[multipart_transfer] Writing to file: " << opts.output_file_path << std::endl;
    }

    // Probe with a tiny ranged GET (bytes=0-0) to fetch headers quickly
    http_client probe;
    probe.set_timeout(opts.per_request_timeout_seconds);
    http_client::request probe_req(url);
    probe_req.headers["Range"] = "bytes=0-0";
    std::cout << "[multipart_transfer] Sending probe Range: " << probe_req.headers["Range"]
              << std::endl;
    auto head_like = probe.get(probe_req);
    std::cout << "[multipart_transfer] Probe completed" << std::endl;

    std::cout << "[multipart_transfer] Probe status=" << head_like.status_code << std::endl;
    auto cl_it = head_like.headers.find("content-length");
    if (cl_it != head_like.headers.end()) {
        std::cout << "[multipart_transfer] content-length=" << cl_it->second << std::endl;
    } else {
        std::cout << "[multipart_transfer] content-length header missing" << std::endl;
    }
    auto ar_it = head_like.headers.find("accept-ranges");
    if (ar_it != head_like.headers.end()) {
        std::cout << "[multipart_transfer] accept-ranges=" << ar_it->second << std::endl;
    } else {
        std::cout << "[multipart_transfer] accept-ranges header missing" << std::endl;
    }

    std::uint64_t total_bytes = parse_content_length(head_like);
    bool ranges = server_supports_ranges(head_like);
    std::cout << "[multipart_transfer] total_bytes=" << total_bytes
              << ", ranges_supported=" << (ranges ? "true" : "false") << std::endl;

    if (total_bytes == 0) {
        // Fallback: perform single request and report all at once
        if (cancel_requested_.load(std::memory_order_relaxed)) {
            if (on_complete)
                on_complete(false, "cancelled");
            return;
        }
        std::cout << "[multipart_transfer] Falling back to single GET due to unknown size"
                  << std::endl;
        auto resp = probe.get(url);
        if (resp.status_code < 200 || resp.status_code >= 300) {
            if (on_complete)
                on_complete(false, "http status " + std::to_string(resp.status_code));
            return;
        }
        buffer_.assign(resp.body.begin(), resp.body.end());
        if (on_progress) {
            progress_info p{0,  buffer_.size(), buffer_.size(), buffer_.size(), buffer_.size(), 0.0,
                            0.0};
            on_progress(p);
        }
        if (on_complete)
            on_complete(true, "");
        return;
    }

    buffer_.assign(total_bytes, 0);
    if (!ranges) {
        // No range support: single shot
        std::cout << "[multipart_transfer] Server does not support ranges. Doing single GET."
                  << std::endl;
        if (cancel_requested_.load(std::memory_order_relaxed)) {
            if (on_complete)
                on_complete(false, "cancelled");
            return;
        }
        auto resp = probe.get(url);
        if (resp.status_code < 200 || resp.status_code >= 300) {
            if (on_complete)
                on_complete(false, "http status " + std::to_string(resp.status_code));
            return;
        }
        if (resp.body.size() != total_bytes)
            buffer_.resize(resp.body.size());
        std::memcpy(buffer_.data(), resp.body.data(), buffer_.size());
        if (on_progress) {
            progress_info p{0,  buffer_.size(), buffer_.size(), buffer_.size(), buffer_.size(), 0.0,
                            0.0};
            on_progress(p);
        }
        if (on_complete)
            on_complete(true, "");
        return;
    }

    // Plan parts and dispatch pool of futures (bounded by max_threads)
    auto parts = plan_parts(total_bytes, opts);
    std::cout << "[multipart_transfer] Planned parts=" << parts.size() << std::endl;
    std::atomic<std::uint64_t> global_written(0);
    auto global_start_tp = std::chrono::steady_clock::now();

    // Use the original URL for all parts (no mirror/effective URL caching)
    std::string base_url_for_parts = url;

    std::vector<std::future<bool>> in_flight;
    std::mutex error_mutex;
    std::string first_error;
    std::atomic<bool> failed(false);

    std::size_t next_index = 0;

    auto launch_next = [&](std::size_t count) {
        for (std::size_t i = 0; i < count && next_index < parts.size(); ++i, ++next_index) {
            const auto range = parts[next_index];
            std::cout << "[multipart_transfer] Launching part index=" << next_index
                      << " range=" << range.start << "-" << range.end_inclusive << std::endl;
            std::size_t part_idx_captured = next_index;
            // Use validated base URL for all parts
            std::string part_url = base_url_for_parts;

            in_flight.emplace_back(std::async(std::launch::async, [&, range, part_idx_captured,
                                                                   part_url]() {
                std::string err;
                bool ok = download_part(part_url, range, part_idx_captured, buffer_, global_written,
                                        on_progress, err, global_start_tp, opts);
                if (!ok) {
                    std::lock_guard<std::mutex> lk(error_mutex);
                    if (!failed.exchange(true))
                        first_error = err;
                }
                return ok;
            }));
        }
    };

    launch_next(std::min<std::size_t>(opts.max_threads, parts.size()));

    while (!in_flight.empty()) {
        auto fut = std::move(in_flight.back());
        in_flight.pop_back();
        bool ok = fut.get();
        if (!ok || cancel_requested_.load(std::memory_order_relaxed)) {
            // Stop launching new parts; wait for currently launched to drain
            failed.store(true);
        } else {
            launch_next(1);
        }
    }

    if (cancel_requested_.load(std::memory_order_relaxed)) {
        std::cout << "[multipart_transfer] Cancel detected after transfers" << std::endl;
        if (on_complete)
            on_complete(false, "cancelled");
        return;
    }

    if (failed.load()) {
        std::cout << "[multipart_transfer] One or more parts failed: " << first_error << std::endl;
        if (on_complete)
            on_complete(false, first_error.empty() ? "download failed" : first_error);
        return;
    }

    std::cout << "[multipart_transfer] All parts completed successfully" << std::endl;

    // Write to file if output path is specified
    if (!opts.output_file_path.empty()) {
        std::cout << "[multipart_transfer] Writing to file: " << opts.output_file_path << std::endl;
        std::ofstream file(opts.output_file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[multipart_transfer] Failed to open file for writing: "
                      << opts.output_file_path << std::endl;
            if (on_complete)
                on_complete(false, "Failed to open output file");
            return;
        }
        file.write(reinterpret_cast<const char*>(buffer_.data()), buffer_.size());
        file.close();
        std::cout << "[multipart_transfer] File written successfully: " << opts.output_file_path
                  << std::endl;
    }

    if (on_complete)
        on_complete(true, "");
}
