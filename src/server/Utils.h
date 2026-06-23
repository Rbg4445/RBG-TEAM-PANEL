#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <cstdint>

namespace Utils {
    // Computes SHA-256 hash of a string and returns it as a hex string
    std::string Sha256(const std::string& input);

    // Returns current Unix timestamp in seconds
    int64_t GetCurrentTimestamp();
}

#endif // UTILS_H
