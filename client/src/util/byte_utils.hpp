#pragma once

#include <cstdint>
#include <sstream>
#include <string>

namespace byte_utils {

inline std::string format_bytes(std::uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};

    int unit_index = 0;
    std::uint64_t scale = 1ULL;
    while (unit_index < 5 && bytes >= scale * 1024ULL) {
        scale *= 1024ULL;
        ++unit_index;
    }

    double in_unit = static_cast<double>(bytes) / static_cast<double>(scale);

    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    if (unit_index == 0) {
        oss.precision(0);
    } else {
        oss.precision(in_unit < 10.0 ? 2 : (in_unit < 100.0 ? 1 : 0));
    }

    oss << in_unit << ' ' << units[unit_index];
    return oss.str();
}

} // namespace byte_utils
