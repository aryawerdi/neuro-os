#ifndef NEURO_OS_MEMORY_PREFETCH_POLICY_HPP
#define NEURO_OS_MEMORY_PREFETCH_POLICY_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace neuro_os::memory {

enum class PrefetchAggressiveness {
    Conservative,
    Moderate,
    Aggressive
};

struct QoSHint {
    uint32_t priority_class : 4;
    uint32_t latency_sensitive : 1;
    uint32_t bandwidth_critical : 1;
    uint32_t reserved : 26;
    
    static constexpr QoSHint default_hint() {
        return QoSHint{4, 0, 0, 0};
    }
    
    static constexpr QoSHint high_priority() {
        return QoSHint{7, 1, 1, 0};
    }
    
    static constexpr QoSHint low_priority() {
        return QoSHint{1, 0, 0, 0};
    }
};

struct BandwidthStatus {
    float current_utilization;
    float peak_utilization;
    std::chrono::nanoseconds latency_estimate;
    
    static constexpr BandwidthStatus idle() {
        return BandwidthStatus{0.0f, 0.0f, std::chrono::nanoseconds{0}};
    }
};

class PrefetchPolicy {
public:
    static constexpr size_t DEFAULT_PREFETCH_DISTANCE = 64;
    static constexpr size_t MAX_PREFETCH_DISTANCE = 256;
    static constexpr size_t MIN_PREFETCH_DISTANCE = 4;
    static constexpr float DEFAULT_CONFIDENCE_THRESHOLD = 0.5f;
    static constexpr float AGGRESSIVE_CONFIDENCE_THRESHOLD = 0.3f;
    static constexpr float CONSERVATIVE_CONFIDENCE_THRESHOLD = 0.8f;

private:
    PrefetchAggressiveness aggressiveness_ = PrefetchAggressiveness::Moderate;
    float aggressiveness_level_ = 0.5f;
    size_t prefetch_distance_ = DEFAULT_PREFETCH_DISTANCE;
    float confidence_threshold_ = DEFAULT_CONFIDENCE_THRESHOLD;
    size_t max_pending_prefetches_ = 16;
    BandwidthStatus bandwidth_status_ = BandwidthStatus::idle();
    bool bandwidth_aware_ = true;
    QoSHint qos_hint_ = QoSHint::default_hint();

public:
    PrefetchPolicy() = default;
    explicit PrefetchPolicy(PrefetchAggressiveness level);

    void set_aggressiveness(float level) {
        level = std::clamp(level, 0.0f, 1.0f);
        aggressiveness_level_ = level;
        
        if (level < 0.33f) {
            aggressiveness_ = PrefetchAggressiveness::Conservative;
            confidence_threshold_ = CONSERVATIVE_CONFIDENCE_THRESHOLD;
            prefetch_distance_ = MIN_PREFETCH_DISTANCE;
            max_pending_prefetches_ = 4;
        } else if (level < 0.66f) {
            aggressiveness_ = PrefetchAggressiveness::Moderate;
            confidence_threshold_ = DEFAULT_CONFIDENCE_THRESHOLD;
            prefetch_distance_ = DEFAULT_PREFETCH_DISTANCE;
            max_pending_prefetches_ = 16;
        } else {
            aggressiveness_ = PrefetchAggressiveness::Aggressive;
            confidence_threshold_ = AGGRESSIVE_CONFIDENCE_THRESHOLD;
            prefetch_distance_ = MAX_PREFETCH_DISTANCE;
            max_pending_prefetches_ = 64;
        }
    }

    float get_aggressiveness() const {
        return aggressiveness_level_;
    }

    PrefetchAggressiveness get_aggressiveness_level() const {
        return aggressiveness_;
    }

    void set_prefetch_distance(size_t distance) {
        prefetch_distance_ = std::clamp(distance, MIN_PREFETCH_DISTANCE, MAX_PREFETCH_DISTANCE);
    }

    size_t get_prefetch_distance() const {
        return prefetch_distance_;
    }

    void set_confidence_threshold(float threshold) {
        confidence_threshold_ = std::clamp(threshold, 0.0f, 1.0f);
    }

    float get_confidence_threshold() const {
        return confidence_threshold_;
    }

    bool should_prefetch(float confidence) const {
        return confidence >= confidence_threshold_;
    }

    void set_max_pending_prefetches(size_t max_pending) {
        max_pending_prefetches_ = std::max(size_t(1), max_pending);
    }

    size_t get_max_pending_prefetches() const {
        return max_pending_prefetches_;
    }

    void update_bandwidth_status(const BandwidthStatus& status) {
        bandwidth_status_ = status;
    }

    BandwidthStatus get_bandwidth_status() const {
        return bandwidth_status_;
    }

    void set_bandwidth_aware(bool aware) {
        bandwidth_aware_ = aware;
    }

    bool is_bandwidth_aware() const {
        return bandwidth_aware_;
    }

    bool should_throttle_due_to_bandwidth() const {
        if (!bandwidth_aware_) return false;
        return bandwidth_status_.current_utilization > 0.9f;
    }

    void set_qos_hint(const QoSHint& hint) {
        qos_hint_ = hint;
    }

    QoSHint get_qos_hint() const {
        return qos_hint_;
    }

    size_t calculate_adaptive_distance(float confidence) const {
        float base_distance = static_cast<float>(prefetch_distance_);
        float confidence_factor = std::max(0.5f, confidence);
        return static_cast<size_t>(base_distance * confidence_factor);
    }

    size_t calculate_adaptive_count(float confidence) const {
        float base_count = static_cast<float>(max_pending_prefetches_);
        float confidence_factor = std::max(0.25f, confidence);
        return static_cast<size_t>(base_count * confidence_factor);
    }

    PrefetchPolicy clone() const {
        PrefetchPolicy copy;
        copy.aggressiveness_ = aggressiveness_;
        copy.aggressiveness_level_ = aggressiveness_level_;
        copy.prefetch_distance_ = prefetch_distance_;
        copy.confidence_threshold_ = confidence_threshold_;
        copy.max_pending_prefetches_ = max_pending_prefetches_;
        copy.bandwidth_status_ = bandwidth_status_;
        copy.bandwidth_aware_ = bandwidth_aware_;
        copy.qos_hint_ = qos_hint_;
        return copy;
    }

    void reset() {
        set_aggressiveness(0.5f);
        bandwidth_status_ = BandwidthStatus::idle();
        qos_hint_ = QoSHint::default_hint();
    }
};

inline PrefetchPolicy::PrefetchPolicy(PrefetchAggressiveness level) 
    : aggressiveness_(level) {
    switch (level) {
        case PrefetchAggressiveness::Conservative:
            set_aggressiveness(0.2f);
            break;
        case PrefetchAggressiveness::Moderate:
            set_aggressiveness(0.5f);
            break;
        case PrefetchAggressiveness::Aggressive:
            set_aggressiveness(0.8f);
            break;
    }
}

}

#endif
