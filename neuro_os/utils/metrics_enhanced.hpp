#ifndef NEURO_OS_UTILS_METRICS_ENHANCED_HPP
#define NEURO_OS_UTILS_METRICS_ENHANCED_HPP

#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>

namespace neuro_os::utils {

// Rate limiter for high-frequency metrics
class RateLimiter {
public:
    RateLimiter(size_t max_events_per_second = 1000) 
        : max_events_per_second_(max_events_per_second)
        , last_reset_(std::chrono::steady_clock::now())
        , count_(0) {}
    
    bool allow() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_reset_).count();
        
        if (elapsed >= 1) {
            count_ = 0;
            last_reset_ = now;
        }
        
        if (count_ < max_events_per_second_) {
            ++count_;
            return true;
        }
        return false;
    }
    
private:
    size_t max_events_per_second_;
    std::chrono::steady_clock::time_point last_reset_;
    std::atomic<size_t> count_;
};

// Prometheus-style metric labels
using Labels = std::map<std::string, std::string>;

struct LabelHasher {
    size_t operator()(const Labels& labels) const {
        size_t hash = 0;
        for (const auto& pair : labels) {
            hash ^= std::hash<std::string>{}(pair.first) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<std::string>{}(pair.second) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

struct LabelEqual {
    bool operator()(const Labels& lhs, const Labels& rhs) const {
        return lhs == rhs;
    }
};

// Enhanced Counter with labels and rate limiting
class EnhancedCounter {
public:
    EnhancedCounter(const std::string& name, const Labels& labels = {})
        : name_(name), labels_(labels), value_(0) {}
    
    void increment(double value = 1.0) {
        value_.fetch_add(static_cast<uint64_t>(value), std::memory_order_relaxed);
    }
    
    void reset() {
        value_.store(0, std::memory_order_relaxed);
    }
    
    double value() const {
        return static_cast<double>(value_.load(std::memory_order_relaxed));
    }
    
    const std::string& name() const { return name_; }
    const Labels& labels() const { return labels_; }
    
    std::string to_prometheus() const {
        std::stringstream ss;
        ss << name_;
        if (!labels_.empty()) {
            ss << "{";
            bool first = true;
            for (const auto& [key, value] : labels_) {
                if (!first) ss << ",";
                ss << key << "=\"" << value << "\"";
                first = false;
            }
            ss << "}";
        }
        ss << " " << value();
        return ss.str();
    }
    
private:
    std::string name_;
    Labels labels_;
    std::atomic<uint64_t> value_;
};

// Enhanced Gauge with labels
class EnhancedGauge {
public:
    EnhancedGauge(const std::string& name, const Labels& labels = {})
        : name_(name), labels_(labels), value_(0.0) {}
    
    void set(double value) {
        value_.store(value, std::memory_order_relaxed);
    }
    
    void increment(double value = 1.0) {
        double current = value_.load(std::memory_order_relaxed);
        while (!value_.compare_exchange_weak(current, current + value, 
                                            std::memory_order_relaxed, 
                                            std::memory_order_relaxed)) {}
    }
    
    void decrement(double value = 1.0) {
        increment(-value);
    }
    
    double value() const {
        return value_.load(std::memory_order_relaxed);
    }
    
    const std::string& name() const { return name_; }
    const Labels& labels() const { return labels_; }
    
    std::string to_prometheus() const {
        std::stringstream ss;
        ss << name_;
        if (!labels_.empty()) {
            ss << "{";
            bool first = true;
            for (const auto& [key, value] : labels_) {
                if (!first) ss << ",";
                ss << key << "=\"" << value << "\"";
                first = false;
            }
            ss << "}";
        }
        ss << " " << std::fixed << std::setprecision(6) << value();
        return ss.str();
    }
    
private:
    std::string name_;
    Labels labels_;
    std::atomic<double> value_;
};

// Enhanced Histogram with configurable buckets and aggregation
class EnhancedHistogram {
public:
    struct Bucket {
        double upper_bound;
        uint64_t count;
        std::string to_prometheus(const std::string& name, const Labels& labels) const {
            std::stringstream ss;
            ss << name << "_bucket{";
            bool first = true;
            for (const auto& [key, value] : labels) {
                if (!first) ss << ",";
                ss << key << "=\"" << value << "\"";
                first = false;
            }
            if (!labels.empty()) ss << ",";
            ss << "le=\"" << (upper_bound == std::numeric_limits<double>::infinity() ? "+Inf" : std::to_string(upper_bound)) << "\"} " << count;
            return ss.str();
        }
    };
    
    struct Stats {
        double count{0.0};
        double sum{0.0};
        double mean{0.0};
        double min{std::numeric_limits<double>::max()};
        double max{std::numeric_limits<double>::lowest()};
        double stddev{0.0};
        std::vector<double> percentiles;
        
        void update(double value) {
            count += 1.0;
            sum += value;
            mean = sum / count;
            min = std::min(min, value);
            max = std::max(max, value);
            
            // Update variance for stddev calculation
            static thread_local double m2 = 0.0;
            double delta = value - mean;
            m2 += delta * delta;
            if (count > 1) {
                stddev = std::sqrt(m2 / (count - 1));
            }
        }
    };
    
    EnhancedHistogram(const std::string& name, const Labels& labels = {}, 
                     const std::vector<double>& buckets = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0})
        : name_(name), labels_(labels), buckets_(buckets.size() + 1) {
        // Sort and add +Inf bucket
        std::vector<double> sorted_buckets = buckets;
        std::sort(sorted_buckets.begin(), sorted_buckets.end());
        for (size_t i = 0; i < sorted_buckets.size(); ++i) {
            buckets_[i] = {sorted_buckets[i], 0};
        }
        buckets_.back() = {std::numeric_limits<double>::infinity(), 0};
    }
    
    void observe(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Update total and count
        total_.fetch_add(value, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);
        
        // Update buckets
        for (auto& bucket : buckets_) {
            if (value <= bucket.upper_bound) {
                ++bucket.count;
                break;
            }
        }
        
        // Update stats
        stats_.update(value);
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        total_.store(0.0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
        for (auto& bucket : buckets_) {
            bucket.count = 0;
        }
        stats_ = Stats{};
    }
    
    const std::string& name() const { return name_; }
    const Labels& labels() const { return labels_; }
    const std::vector<Bucket>& buckets() const { return buckets_; }
    
    double sum() const { return total_.load(std::memory_order_relaxed); }
    uint64_t count() const { return count_.load(std::memory_order_relaxed); }
    
    std::vector<std::string> to_prometheus() const {
        std::vector<std::string> result;
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Add bucket metrics
        uint64_t cumulative_count = 0;
        for (const auto& bucket : buckets_) {
            cumulative_count += bucket.count;
            result.push_back(bucket.to_prometheus(name_, labels_));
        }
        
        // Add sum metric
        std::stringstream ss_sum;
        ss_sum << name_ << "_sum{";
        bool first = true;
        for (const auto& [key, value] : labels_) {
            if (!first) ss_sum << ",";
            ss_sum << key << "=\"" << value << "\"";
            first = false;
        }
        ss_sum << "} " << total_.load(std::memory_order_relaxed);
        result.push_back(ss_sum.str());
        
        // Add count metric
        std::stringstream ss_count;
        ss_count << name_ << "_count{";
        first = true;
        for (const auto& [key, value] : labels_) {
            if (!first) ss_count << ",";
            ss_count << key << "=\"" << value << "\"";
            first = false;
        }
        ss_count << "} " << count_.load(std::memory_order_relaxed);
        result.push_back(ss_count.str());
        
        return result;
    }
    
    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }
    
private:
    std::string name_;
    Labels labels_;
    std::vector<Bucket> buckets_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> count_{0};
    std::atomic<double> total_{0.0};
    Stats stats_;
};

// Metric aggregation (sum, avg, min, max over time windows)
class MetricAggregator {
public:
    enum class AggregationType {
        SUM,
        AVG,
        MIN,
        MAX,
        COUNT
    };
    
    struct AggregatedValue {
        double value{0.0};
        std::chrono::system_clock::time_point timestamp;
        AggregationType type;
    };
    
    MetricAggregator(std::chrono::seconds window_size = std::chrono::seconds(60))
        : window_size_(window_size) {}
    
    void add_value(double value, AggregationType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        
        // Clean old values
        clean_old_values(now);
        
        // Add new value
        values_.push_back({value, now, type});
        
        // Update aggregated value
        update_aggregated_value();
    }
    
    std::optional<AggregatedValue> get_aggregated() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (values_.empty()) {
            return std::nullopt;
        }
        return aggregated_;
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.clear();
        aggregated_ = AggregatedValue{};
    }
    
private:
    void clean_old_values(std::chrono::system_clock::time_point now) {
        auto cutoff = now - window_size_;
        values_.erase(
            std::remove_if(values_.begin(), values_.end(),
                [cutoff](const AggregatedValue& v) {
                    return v.timestamp < cutoff;
                }),
            values_.end()
        );
    }
    
    void update_aggregated_value() {
        if (values_.empty()) {
            aggregated_ = AggregatedValue{};
            return;
        }
        
        aggregated_.timestamp = values_.back().timestamp;
        aggregated_.type = values_.front().type;
        
        switch (aggregated_.type) {
            case AggregationType::SUM:
                aggregated_.value = 0.0;
                for (const auto& v : values_) aggregated_.value += v.value;
                break;
            case AggregationType::AVG:
                aggregated_.value = 0.0;
                for (const auto& v : values_) aggregated_.value += v.value;
                aggregated_.value /= values_.size();
                break;
            case AggregationType::MIN:
                aggregated_.value = std::numeric_limits<double>::max();
                for (const auto& v : values_) aggregated_.value = std::min(aggregated_.value, v.value);
                break;
            case AggregationType::MAX:
                aggregated_.value = std::numeric_limits<double>::lowest();
                for (const auto& v : values_) aggregated_.value = std::max(aggregated_.value, v.value);
                break;
            case AggregationType::COUNT:
                aggregated_.value = static_cast<double>(values_.size());
                break;
        }
    }
    
    std::chrono::seconds window_size_;
    mutable std::mutex mutex_;
    std::vector<AggregatedValue> values_;
    AggregatedValue aggregated_;
};

// Enhanced Metrics Registry with all features
class EnhancedMetricsRegistry {
public:
    static EnhancedMetricsRegistry& instance() {
        static EnhancedMetricsRegistry instance;
        return instance;
    }
    
    // Counter management
    EnhancedCounter& get_counter(const std::string& name, const Labels& labels = {}) {
        std::string key = name + label_key(labels);
        {
            std::shared_lock lock(mutex_);
            auto it = counters_.find(key);
            if (it != counters_.end()) {
                return *it->second;
            }
        }
        {
            std::unique_lock lock(mutex_);
            if (counters_.find(key) == counters_.end()) {
                counters_[key] = std::make_unique<EnhancedCounter>(name, labels);
            }
            return *counters_[key];
        }
    }
    
    // Gauge management
    EnhancedGauge& get_gauge(const std::string& name, const Labels& labels = {}) {
        std::string key = name + label_key(labels);
        {
            std::shared_lock lock(mutex_);
            auto it = gauges_.find(key);
            if (it != gauges_.end()) {
                return *it->second;
            }
        }
        {
            std::unique_lock lock(mutex_);
            if (gauges_.find(key) == gauges_.end()) {
                gauges_[key] = std::make_unique<EnhancedGauge>(name, labels);
            }
            return *gauges_[key];
        }
    }
    
    // Histogram management
    EnhancedHistogram& get_histogram(const std::string& name, const Labels& labels = {}, 
                                    const std::vector<double>& buckets = {}) {
        std::string key = name + label_key(labels);
        {
            std::shared_lock lock(mutex_);
            auto it = histograms_.find(key);
            if (it != histograms_.end()) {
                return *it->second;
            }
        }
        {
            std::unique_lock lock(mutex_);
            if (histograms_.find(key) == histograms_.end()) {
                histograms_[key] = std::make_unique<EnhancedHistogram>(name, labels, buckets);
            }
            return *histograms_[key];
        }
    }
    
    // Rate-limited metric observation
    bool observe_rate_limited(const std::string& name, double value, const Labels& labels = {}) {
        std::string key = name + label_key(labels);
        {
            std::shared_lock lock(rate_limit_mutex_);
            auto it = rate_limiters_.find(key);
            if (it != rate_limiters_.end() && !it->second->allow()) {
                return false;
            }
        }
        {
            std::unique_lock lock(rate_limit_mutex_);
            if (rate_limiters_.find(key) == rate_limiters_.end()) {
                rate_limiters_[key] = std::make_unique<RateLimiter>();
            }
            if (!rate_limiters_[key]->allow()) {
                return false;
            }
        }
        
        // Get or create histogram and observe
        get_histogram(name, labels).observe(value);
        return true;
    }
    
    // Export metrics in Prometheus format
    std::string export_prometheus() const {
        std::stringstream ss;
        ss << "# TYPE neuroos_counter counter\n";
        {
            std::shared_lock lock(mutex_);
            for (const auto& [key, counter] : counters_) {
                ss << counter->to_prometheus() << "\n";
            }
        }
        
        ss << "\n# TYPE neuroos_gauge gauge\n";
        {
            std::shared_lock lock(mutex_);
            for (const auto& [key, gauge] : gauges_) {
                ss << gauge->to_prometheus() << "\n";
            }
        }
        
        ss << "\n# TYPE neuroos_histogram histogram\n";
        {
            std::shared_lock lock(mutex_);
            for (const auto& [key, histogram] : histograms_) {
                auto prometheus_lines = histogram->to_prometheus();
                for (const auto& line : prometheus_lines) {
                    ss << line << "\n";
                }
            }
        }
        
        return ss.str();
    }
    
    // Export metrics in JSON format
    std::string export_json() const {
        std::stringstream ss;
        ss << "{\n";
        
        ss << "  \"counters\": {\n";
        bool first_counter = true;
        {
            std::shared_lock lock(mutex_);
            for (const auto& [key, counter] : counters_) {
                if (!first_counter) ss << ",\n";
                ss << "    \"" << counter->name() << "\": " << counter->value();
                first_counter = false;
            }
        }
        ss << "\n  },\n";
        
        ss << "  \"gauges\": {\n";
        bool first_gauge = true;
        {
            std::shared_lock lock(mutex_);
            for (const auto& [key, gauge] : gauges_) {
                if (!first_gauge) ss << ",\n";
                ss << "    \"" << gauge->name() << "\": " << std::fixed << std::setprecision(6) << gauge->value();
                first_gauge = false;
            }
        }
        ss << "\n  },\n";
        
        ss << "  \"histograms\": {\n";
        bool first_hist = true;
        {
            std::shared_lock lock(mutex_);
            for (const auto& [key, histogram] : histograms_) {
                if (!first_hist) ss << ",\n";
                auto stats = histogram->get_stats();
                ss << "    \"" << histogram->name() << "\": {"
                   << "\"count\": " << stats.count << ", "
                   << "\"sum\": " << stats.sum << ", "
                   << "\"mean\": " << stats.mean << ", "
                   << "\"min\": " << stats.min << ", "
                   << "\"max\": " << stats.max << ", "
                   << "\"stddev\": " << stats.stddev << "}";
                first_hist = false;
            }
        }
        ss << "\n  }\n";
        
        ss << "}\n";
        return ss.str();
    }
    
    void reset() {
        std::unique_lock lock(mutex_);
        counters_.clear();
        gauges_.clear();
        histograms_.clear();
    }
    
private:
    EnhancedMetricsRegistry() = default;
    
    std::string label_key(const Labels& labels) const {
        std::string key;
        for (const auto& [k, v] : labels) {
            key += "|" + k + "=" + v;
        }
        return key;
    }
    
    mutable std::shared_mutex mutex_;
    mutable std::mutex rate_limit_mutex_;
    std::map<std::string, std::unique_ptr<EnhancedCounter>> counters_;
    std::map<std::string, std::unique_ptr<EnhancedGauge>> gauges_;
    std::map<std::string, std::unique_ptr<EnhancedHistogram>> histograms_;
    std::map<std::string, std::unique_ptr<RateLimiter>> rate_limiters_;
};

// Convenience functions
inline EnhancedCounter& get_enhanced_counter(const std::string& name, const Labels& labels = {}) {
    return EnhancedMetricsRegistry::instance().get_counter(name, labels);
}

inline EnhancedGauge& get_enhanced_gauge(const std::string& name, const Labels& labels = {}) {
    return EnhancedMetricsRegistry::instance().get_gauge(name, labels);
}

inline EnhancedHistogram& get_enhanced_histogram(const std::string& name, const Labels& labels = {}, 
                                                const std::vector<double>& buckets = {}) {
    return EnhancedMetricsRegistry::instance().get_histogram(name, labels, buckets);
}

inline bool observe_rate_limited(const std::string& name, double value, const Labels& labels = {}) {
    return EnhancedMetricsRegistry::instance().observe_rate_limited(name, value, labels);
}

inline std::string export_enhanced_metrics_prometheus() {
    return EnhancedMetricsRegistry::instance().export_prometheus();
}

inline std::string export_enhanced_metrics_json() {
    return EnhancedMetricsRegistry::instance().export_json();
}

}

#endif // NEURO_OS_UTILS_METRICS_ENHANCED_HPP