#pragma once

#include <string>
#include <utility>

namespace hm_ld1::internal {

inline void ClearError(std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
}

inline void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

}  // namespace hm_ld1::internal
