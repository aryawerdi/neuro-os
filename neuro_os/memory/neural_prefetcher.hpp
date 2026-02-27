#ifndef NEURO_OS_MEMORY_NEURAL_PREFETCHER_HPP
#define NEURO_OS_MEMORY_NEURAL_PREFETCHER_HPP

#include "access_predictor.hpp"
#include "prefetch_policy.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <numeric>
#include <optional>
#include <vector>

namespace neuro_os::memory {

using namespace std::chrono_literals;

struct PrefetchHint {
    void* address;
    size_t size;
    int priority;
    float confidence;
    AccessPredictor::PatternType predicted_pattern;
};

class NeuralPrefetcher {
private:
    static constexpr size_t DEFAULT_LOOKAHEAD = 8;
    static constexpr size_t MAX_HISTORY = 256;
    static constexpr size_t PATTERN_WINDOW = 16;

    AccessPredictor predictor_;
    PrefetchPolicy policy_;

    std::deque<int64_t> stride_history_;
    std::deque<int64_t> address_history_;
    std::deque<size_t> size_history_;
    std::deque<int64_t> timestamp_history_;

    int64_t last_address_ = 0;
    size_t last_size_ = 0;
    int64_t last_stride_ = 0;

    bool trained_ = false;
    float model_confidence_ = 0.0f;

    std::vector<PrefetchHint> last_hints_;

public:
    NeuralPrefetcher() {
        set_aggressiveness(0.5f);
    }

    explicit NeuralPrefetcher(float aggressiveness) {
        set_aggressiveness(aggressiveness);
    }

    std::vector<PrefetchHint> predict(const AccessPattern& pattern) {
        auto detected = predictor_.detect(pattern);
        
        if (pattern.addresses.empty()) {
            return {};
        }

        std::vector<PrefetchHint> hints;

        switch (detected) {
            case AccessPredictor::PatternType::Sequential:
                hints = predict_sequential(pattern);
                break;
            case AccessPredictor::PatternType::Strided:
                hints = predict_strided(pattern);
                break;
            case AccessPredictor::PatternType::Random:
            case AccessPredictor::PatternType::Irregular:
                hints = predict_random(pattern);
                break;
        }

        hints = filter_by_policy(hints);
        
        last_hints_ = hints;
        return hints;
    }

    void train(const AccessPattern& observed) {
        if (observed.addresses.size() < 2) {
            return;
        }

        for (size_t i = 1; i < observed.addresses.size(); ++i) {
            int64_t curr = reinterpret_cast<int64_t>(observed.addresses[i]);
            int64_t prev = reinterpret_cast<int64_t>(observed.addresses[i - 1]);
            int64_t stride = curr - prev;
            
            stride_history_.push_back(stride);
            if (stride_history_.size() > MAX_HISTORY) {
                stride_history_.pop_front();
            }
        }

        for (const auto& addr : observed.addresses) {
            address_history_.push_back(reinterpret_cast<int64_t>(addr));
        }
        while (address_history_.size() > MAX_HISTORY) {
            address_history_.pop_front();
        }

        for (const auto& size : observed.sizes) {
            size_history_.push_back(size);
        }
        while (size_history_.size() > MAX_HISTORY) {
            size_history_.pop_front();
        }

        if (!observed.timestamps_ns.empty()) {
            for (const auto& ts : observed.timestamps_ns) {
                timestamp_history_.push_back(ts);
            }
            while (timestamp_history_.size() > MAX_HISTORY) {
                timestamp_history_.pop_front();
            }
        }

        if (address_history_.size() >= 2) {
            last_address_ = address_history_.back();
            last_stride_ = address_history_.back() - address_history_[address_history_.size() - 2];
        }
        if (!size_history_.empty()) {
            last_size_ = size_history_.back();
        }

        model_confidence_ = calculate_training_confidence();
        trained_ = true;

        predictor_.detect(observed);
    }

    void set_aggressiveness(float level) {
        policy_.set_aggressiveness(level);
    }

    float get_aggressiveness() const {
        return policy_.get_aggressiveness();
    }

    void set_prefetch_distance(size_t distance) {
        policy_.set_prefetch_distance(distance);
    }

    size_t get_prefetch_distance() const {
        return policy_.get_prefetch_distance();
    }

    PrefetchPolicy& get_policy() {
        return policy_;
    }

    const PrefetchPolicy& get_policy() const {
        return policy_;
    }

    void set_qos_hint(const QoSHint& hint) {
        policy_.set_qos_hint(hint);
    }

    AccessPredictor& get_predictor() {
        return predictor_;
    }

    const AccessPredictor& get_predictor() const {
        return predictor_;
    }

    bool is_trained() const {
        return trained_;
    }

    float get_confidence() const {
        return model_confidence_;
    }

    const std::vector<PrefetchHint>& get_last_hints() const {
        return last_hints_;
    }

    void reset() {
        stride_history_.clear();
        address_history_.clear();
        size_history_.clear();
        timestamp_history_.clear();
        last_address_ = 0;
        last_size_ = 0;
        last_stride_ = 0;
        trained_ = false;
        model_confidence_ = 0.0f;
        last_hints_.clear();
        predictor_.reset();
        policy_.reset();
    }

private:
    std::vector<PrefetchHint> predict_sequential(const AccessPattern& pattern) {
        std::vector<PrefetchHint> hints;
        
        if (address_history_.size() < 2) {
            return hints;
        }

        int64_t stride = address_history_.back() - address_history_[address_history_.size() - 2];
        size_t size = last_size_ > 0 ? last_size_ : 64;
        
        if (stride <= 0) {
            stride = size;
        }

        size_t distance = policy_.get_prefetch_distance();
        float confidence = predictor_.get_confidence();

        for (size_t i = 1; i <= distance; ++i) {
            int64_t next_addr = last_address_ + (stride * static_cast<int64_t>(i));
            
            PrefetchHint hint;
            hint.address = reinterpret_cast<void*>(next_addr);
            hint.size = size;
            hint.priority = static_cast<int>((distance - i + 1) * policy_.get_qos_hint().priority_class);
            hint.confidence = confidence * (1.0f - static_cast<float>(i) / (distance + 1));
            hint.predicted_pattern = AccessPredictor::PatternType::Sequential;
            
            hints.push_back(hint);
        }

        return hints;
    }

    std::vector<PrefetchHint> predict_strided(const AccessPattern& pattern) {
        std::vector<PrefetchHint> hints;
        
        if (stride_history_.empty() || address_history_.empty()) {
            return hints;
        }

        int64_t stride = calculate_average_stride();
        size_t size = last_size_ > 0 ? last_size_ : 64;
        
        if (stride == 0) {
            return hints;
        }

        size_t distance = policy_.calculate_adaptive_distance(model_confidence_);
        float confidence = predictor_.get_confidence();

        int64_t next_addr = last_address_ + stride;
        
        PrefetchHint hint;
        hint.address = reinterpret_cast<void*>(next_addr);
        hint.size = size;
        hint.priority = 5 * policy_.get_qos_hint().priority_class;
        hint.confidence = confidence;
        hint.predicted_pattern = AccessPredictor::PatternType::Strided;
        
        hints.push_back(hint);

        for (size_t i = 2; i <= std::min(distance / 2, size_t(4)); ++i) {
            next_addr += stride;
            
            PrefetchHint subsequent_hint;
            subsequent_hint.address = reinterpret_cast<void*>(next_addr);
            subsequent_hint.size = size;
            subsequent_hint.priority = static_cast<int>(3 * policy_.get_qos_hint().priority_class);
            subsequent_hint.confidence = confidence * 0.8f;
            subsequent_hint.predicted_pattern = AccessPredictor::PatternType::Strided;
            
            hints.push_back(subsequent_hint);
        }

        return hints;
    }

    std::vector<PrefetchHint> predict_random(const AccessPattern& pattern) {
        std::vector<PrefetchHint> hints;
        
        if (!trained_ || address_history_.size() < 4) {
            return hints;
        }

        if (policy_.get_aggressiveness() < 0.6f) {
            return hints;
        }

        auto frequent_addrs = find_frequent_addresses();
        
        for (const auto& addr : frequent_addrs) {
            PrefetchHint hint;
            hint.address = reinterpret_cast<void*>(addr);
            hint.size = last_size_ > 0 ? last_size_ : 64;
            hint.priority = 2;
            hint.confidence = 0.3f;
            hint.predicted_pattern = AccessPredictor::PatternType::Random;
            
            hints.push_back(hint);
        }

        return hints;
    }

    std::vector<PrefetchHint> filter_by_policy(std::vector<PrefetchHint>& hints) {
        if (policy_.should_throttle_due_to_bandwidth()) {
            hints.clear();
            return hints;
        }

        float threshold = policy_.get_confidence_threshold();
        
        std::erase_if(hints, [threshold](const PrefetchHint& hint) {
            return hint.confidence < threshold;
        });

        size_t max_count = policy_.get_max_pending_prefetches();
        if (hints.size() > max_count) {
            hints.resize(max_count);
        }

        return hints;
    }

    int64_t calculate_average_stride() const {
        if (stride_history_.empty()) {
            return 0;
        }

        int64_t sum = std::accumulate(stride_history_.begin(), 
                                       stride_history_.end(), 
                                       int64_t(0));
        return sum / static_cast<int64_t>(stride_history_.size());
    }

    float calculate_training_confidence() const {
        if (stride_history_.empty()) {
            return 0.0f;
        }

        int64_t avg_stride = calculate_average_stride();
        if (avg_stride == 0) {
            return 0.2f;
        }

        float variance = 0.0f;
        for (const auto& stride : stride_history_) {
            float diff = static_cast<float>(stride - avg_stride);
            variance += diff * diff;
        }
        variance /= stride_history_.size();
        
        float stddev = std::sqrt(variance);
        float cv = (avg_stride != 0) ? stddev / std::abs(static_cast<float>(avg_stride)) : 1.0f;
        
        float confidence = 1.0f - std::min(1.0f, cv);
        
        return confidence;
    }

    std::vector<int64_t> find_frequent_addresses() const {
        if (address_history_.size() < 4) {
            return {};
        }

        std::map<int64_t, int> counts;
        for (const auto& addr : address_history_) {
            ++counts[addr];
        }

        std::vector<std::pair<int64_t, int>> sorted(counts.begin(), counts.end());
        std::sort(sorted.begin(), sorted.end(), 
                 [](const auto& a, const auto& b) {
                     return a.second > b.second;
                 });

        std::vector<int64_t> result;
        for (size_t i = 0; i < std::min(sorted.size(), size_t(3)); ++i) {
            if (sorted[i].second >= 3) {
                result.push_back(sorted[i].first);
            }
        }

        return result;
    }
};

}

#endif
