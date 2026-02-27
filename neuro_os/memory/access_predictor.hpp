#ifndef NEURO_OS_MEMORY_ACCESS_PREDICTOR_HPP
#define NEURO_OS_MEMORY_ACCESS_PREDICTOR_HPP

#include <vector>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <map>

namespace neuro_os::memory {

struct AccessPattern {
    std::vector<void*> addresses;
    std::vector<size_t> sizes;
    std::vector<int64_t> timestamps_ns;
};

class AccessPredictor {
public:
    enum class PatternType {
        Sequential,
        Strided,
        Random,
        Irregular
    };

private:
    static constexpr size_t MIN_SEQUENCE_LENGTH = 4;
    static constexpr size_t MAX_STRIDE_HISTORY = 32;
    static constexpr float STRIDE_CONFIDENCE_THRESHOLD = 0.7f;

    std::vector<int64_t> stride_history_;
    std::vector<int64_t> address_history_;
    PatternType last_detected_ = PatternType::Random;
    float confidence_ = 0.0f;
    int64_t detected_stride_ = 0;

public:
    AccessPredictor() = default;

    PatternType detect(const AccessPattern& pattern) {
        if (pattern.addresses.size() < MIN_SEQUENCE_LENGTH) {
            last_detected_ = PatternType::Random;
            confidence_ = 0.0f;
            detected_stride_ = 0;
            return last_detected_;
        }

        for (const auto& addr : pattern.addresses) {
            address_history_.push_back(reinterpret_cast<int64_t>(addr));
        }

        if (address_history_.size() > 256) {
            address_history_.erase(address_history_.begin(), 
                                   address_history_.end() - 256);
        }

        if (is_sequential_pattern()) {
            last_detected_ = PatternType::Sequential;
            confidence_ = calculate_sequential_confidence();
            detected_stride_ = calculate_sequential_stride();
        } else if (is_strided_pattern()) {
            last_detected_ = PatternType::Strided;
            confidence_ = calculate_stride_confidence();
            detected_stride_ = calculate_detected_stride();
        } else {
            last_detected_ = PatternType::Random;
            confidence_ = 0.0f;
            detected_stride_ = 0;
        }

        return last_detected_;
    }

    int64_t detect_stride() const {
        return detected_stride_;
    }

    bool is_sequential() const {
        return last_detected_ == PatternType::Sequential;
    }

    PatternType get_last_type() const {
        return last_detected_;
    }

    float get_confidence() const {
        return confidence_;
    }

    void reset() {
        stride_history_.clear();
        address_history_.clear();
        last_detected_ = PatternType::Random;
        confidence_ = 0.0f;
        detected_stride_ = 0;
    }

private:
    bool is_sequential_pattern() const {
        if (address_history_.size() < 3) return false;

        auto stride = address_history_[1] - address_history_[0];
        for (size_t i = 2; i < address_history_.size(); ++i) {
            auto current_stride = address_history_[i] - address_history_[i - 1];
            if (current_stride != stride) {
                return false;
            }
        }
        return true;
    }

    bool is_strided_pattern() const {
        if (address_history_.size() < 4) return false;

        std::vector<int64_t> strides;
        for (size_t i = 1; i < address_history_.size(); ++i) {
            strides.push_back(address_history_[i] - address_history_[i - 1]);
        }

        if (strides.empty()) return false;

        auto mode = find_mode(strides);
        int64_t mode_count = 0;
        for (const auto& s : strides) {
            if (s == mode) ++mode_count;
        }

        float ratio = static_cast<float>(mode_count) / strides.size();
        return ratio >= 0.5f;
    }

    int64_t find_mode(const std::vector<int64_t>& values) const {
        if (values.empty()) return 0;
        
        std::map<int64_t, int> counts;
        for (const auto& v : values) {
            ++counts[v];
        }
        
        int64_t mode = values[0];
        int max_count = 0;
        for (const auto& [val, cnt] : counts) {
            if (cnt > max_count) {
                max_count = cnt;
                mode = val;
            }
        }
        return mode;
    }

    int64_t calculate_sequential_stride() const {
        if (address_history_.size() < 2) return 0;
        return address_history_[1] - address_history_[0];
    }

    float calculate_sequential_confidence() const {
        if (address_history_.size() < 3) return 0.3f;
        return 0.95f;
    }

    float calculate_stride_confidence() const {
        if (address_history_.size() < 4) return 0.3f;
        
        std::vector<int64_t> strides;
        for (size_t i = 1; i < address_history_.size(); ++i) {
            strides.push_back(address_history_[i] - address_history_[i - 1]);
        }

        auto mode = find_mode(strides);
        int64_t mode_count = 0;
        for (const auto& s : strides) {
            if (s == mode) ++mode_count;
        }

        float ratio = static_cast<float>(mode_count) / strides.size();
        
        if (mode != 0) {
            float stride_magnitude_confidence = std::min(1.0f, 64.0f / std::abs(mode));
            return ratio * stride_magnitude_confidence;
        }
        
        return ratio * 0.5f;
    }

    int64_t calculate_detected_stride() const {
        if (address_history_.size() < 2) return 0;

        std::vector<int64_t> strides;
        for (size_t i = 1; i < address_history_.size(); ++i) {
            strides.push_back(address_history_[i] - address_history_[i - 1]);
        }

        return find_mode(strides);
    }
};

}

#endif
