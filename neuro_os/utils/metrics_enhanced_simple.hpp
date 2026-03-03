#ifndef NEURO_OS_UTILS_METRICS_ENHANCED_SIMPLE_HPP
#define NEURO_OS_UTILS_METRICS_ENHANCED_SIMPLE_HPP

#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
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

// Enhanced Counter with labels
class EnhancedCounter {
public:
    EnhancedCounter(const std::string& name, const Labels& labels = Labels())
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
            for (Labels::const_iterator it = labels_.begin(); it != labels_.end(); ++it) {
                if (!first) ss << ",";
                ss << it->first << "=\"" << it->second << "\"";
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
    EnhancedGauge(const std::string& name, const Labels& labels = Labels())
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
            for (Labels::const_iterator it = labels_.begin(); it != labels_.end(); ++it) {
                if (!first) ss << ",";
                ss << it->first << "=\"" << it->second << "\"";
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

// Enhanced Histogram with configurable buckets
class EnhancedHistogram {
public:
    struct Bucket {
        double upper_bound;
        uint64_t count;
        
        std::string to_prometheus(const std::string& name, const Labels& labels) const {
            std::stringstream ss;
            ss << name << "_bucket{";
            bool first = true;
            for (Labels::const_iterator it = labels.begin(); it != labels.end(); ++it) {
                if (!first) ss << ",";
                ss << it->first << "=\"" << it->second << "\"";
                first = false;
            }
            if (!labels.empty()) ss << ",";
            ss << "le=\"";
            if (upper_bound == std::numeric_limits<double>::infinity()) {
                ss << "+Inf";
            } else {
                ss << upper_bound;
            }
            ss << "\"} " << count;
            return ss.str();
        }
    };
    
    struct Stats {
        double count;
        double sum;
        double mean;
        double min;
        double max;
        double stddev;
        
        Stats() : count(0.0), sum(0.0), mean(0.0), 
                 min(std::numeric_limits<double>::max()), 
                 max(std::numeric_limits<double>::lowest()), 
                 stddev(0.0) {}
    };
    
    EnhancedHistogram(const std::string& name, const Labels& labels = Labels(), 
                     const std::vector<double>& buckets = get_default_buckets())
        : name_(name), labels_(labels) {
        // Sort and add +Inf bucket
        std::vector<double> sorted_buckets = buckets;
        std::sort(sorted_buckets.begin(), sorted_buckets.end());
        for (size_t i = 0; i < sorted_buckets.size(); ++i) {
            Bucket b;
            b.upper_bound = sorted_buckets[i];
            b.count = 0;
            buckets_.push_back(b);
        }
        Bucket inf_bucket;
        inf_bucket.upper_bound = std::numeric_limits<double>::infinity();
        inf_bucket.count = 0;
        buckets_.push_back(inf_bucket);
    }
    
    static std::vector<double> get_default_buckets() {
        std::vector<double> buckets;
        buckets.push_back(0.005);
        buckets.push_back(0.01);
        buckets.push_back(0.025);
        buckets.push_back(0.05);
        buckets.push_back(0.1);
        buckets.push_back(0.25);
        buckets.push_back(0.5);
        buckets.push_back(1.0);
        buckets.push_back(2.5);
        buckets.push_back(5.0);
        buckets.push_back(10.0);
        return buckets;
    }
    
    void observe(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Update total and count
        total_ += value;
        ++count_;
        
        // Update buckets
        for (size_t i = 0; i < buckets_.size(); ++i) {
            if (value <= buckets_[i].upper_bound) {
                ++buckets_[i].count;
                break;
            }
        }
        
        // Update stats
        update_stats(value);
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        total_ = 0.0;
        count_ = 0;
        for (size_t i = 0; i < buckets_.size(); ++i) {
            buckets_[i].count = 0;
        }
        stats_ = Stats();
    }
    
    const std::string& name() const { return name_; }
    const Labels& labels() const { return labels_; }
    const std::vector<Bucket>& buckets() const { return buckets_; }
    
    double sum() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return total_; 
    }
    
    uint64_t get_count() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return count_; 
    }
    
    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }
    
    std::vector<std::string> to_prometheus() const {
        std::vector<std::string> result;
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Add bucket metrics
        uint64_t cumulative_count = 0;
        for (size_t i = 0; i < buckets_.size(); ++i) {
            cumulative_count += buckets_[i].count;
            result.push_back(buckets_[i].to_prometheus(name_, labels_));
        }
        
        // Add sum metric
        std::stringstream ss_sum;
        ss_sum << name_ << "_sum{";
        bool first = true;
        for (Labels::const_iterator it = labels_.begin(); it != labels_.end(); ++it) {
            if (!first) ss_sum << ",";
            ss_sum << it->first << "=\"" << it->second << "\"";
            first = false;
        }
        ss_sum << "} " << total_;
        result.push_back(ss_sum.str());
        
        // Add count metric
        std::stringstream ss_count;
        ss_count << name_ << "_count{";
        first = true;
        for (Labels::const_iterator it = labels_.begin(); it != labels_.end(); ++it) {
            if (!first) ss_count << ",";
            ss_count << it->first << "=\"" << it->second << "\"";
            first = false;
        }
        ss_count << "} " << count_;
        result.push_back(ss_count.str());
        
        return result;
    }
    
private:
    void update_stats(double value) {
        stats_.count += 1.0;
        stats_.sum += value;
        stats_.mean = stats_.sum / stats_.count;
        stats_.min = std::min(stats_.min, value);
        stats_.max = std::max(stats_.max, value);
        
        // Simple stddev calculation (not Welford's algorithm for simplicity)
        // In production, you might want to use a more robust algorithm
        static double m2 = 0.0;
        double delta = value - stats_.mean;
        m2 += delta * delta;
        if (stats_.count > 1) {
            stats_.stddev = std::sqrt(m2 / (stats_.count - 1));
        }
    }
    
    std::string name_;
    Labels labels_;
    std::vector<Bucket> buckets_;
    mutable std::mutex mutex_;
    uint64_t count_{0};
    double total_{0.0};
    Stats stats_;
};

// Enhanced Metrics Registry
class EnhancedMetricsRegistry {
public:
    static EnhancedMetricsRegistry& instance() {
        static EnhancedMetricsRegistry instance;
        return instance;
    }
    
    // Counter management
    EnhancedCounter& get_counter(const std::string& name, const Labels& labels = Labels()) {
        std::string key = name + label_key(labels);
        {
            std::lock_guard<std::mutex> lock(counter_mutex_);
            std::map<std::string, std::unique_ptr<EnhancedCounter> >::iterator it = counters_.find(key);
            if (it != counters_.end()) {
                return *(it->second);
            }
            // Not found, create it
            counters_[key].reset(new EnhancedCounter(name, labels));
            return *counters_[key];
        }
    }
    
    // Gauge management
    EnhancedGauge& get_gauge(const std::string& name, const Labels& labels = Labels()) {
        std::string key = name + label_key(labels);
        {
            std::lock_guard<std::mutex> lock(gauge_mutex_);
            std::map<std::string, std::unique_ptr<EnhancedGauge> >::iterator it = gauges_.find(key);
            if (it != gauges_.end()) {
                return *(it->second);
            }
            // Not found, create it
            gauges_[key].reset(new EnhancedGauge(name, labels));
            return *gauges_[key];
        }
    }
    
    // Histogram management
    EnhancedHistogram& get_histogram(const std::string& name, const Labels& labels = Labels(), 
                                    const std::vector<double>& buckets = EnhancedHistogram::get_default_buckets()) {
        std::string key = name + label_key(labels);
        {
            std::lock_guard<std::mutex> lock(histogram_mutex_);
            std::map<std::string, std::unique_ptr<EnhancedHistogram> >::iterator it = histograms_.find(key);
            if (it != histograms_.end()) {
                return *(it->second);
            }
            // Not found, create it
            histograms_[key].reset(new EnhancedHistogram(name, labels, buckets));
            return *histograms_[key];
        }
    }
    
    // Rate-limited metric observation
    bool observe_rate_limited(const std::string& name, double value, const Labels& labels = Labels()) {
        std::string key = name + label_key(labels);
        {
            std::lock_guard<std::mutex> lock(rate_limit_mutex_);
            std::map<std::string, std::unique_ptr<RateLimiter> >::iterator it = rate_limiters_.find(key);
            if (it != rate_limiters_.end() && !it->second->allow()) {
                return false;
            }
            if (rate_limiters_.find(key) == rate_limiters_.end()) {
                rate_limiters_[key].reset(new RateLimiter());
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
            std::lock_guard<std::mutex> lock(counter_mutex_);
            for (std::map<std::string, std::unique_ptr<EnhancedCounter> >::const_iterator it = counters_.begin(); 
                 it != counters_.end(); ++it) {
                ss << it->second->to_prometheus() << "\n";
            }
        }
        
        ss << "\n# TYPE neuroos_gauge gauge\n";
        {
            std::lock_guard<std::mutex> lock(gauge_mutex_);
            for (std::map<std::string, std::unique_ptr<EnhancedGauge> >::const_iterator it = gauges_.begin(); 
                 it != gauges_.end(); ++it) {
                ss << it->second->to_prometheus() << "\n";
            }
        }
        
        ss << "\n# TYPE neuroos_histogram histogram\n";
        {
            std::lock_guard<std::mutex> lock(histogram_mutex_);
            for (std::map<std::string, std::unique_ptr<EnhancedHistogram> >::const_iterator it = histograms_.begin(); 
                 it != histograms_.end(); ++it) {
                std::vector<std::string> prometheus_lines = it->second->to_prometheus();
                for (size_t i = 0; i < prometheus_lines.size(); ++i) {
                    ss << prometheus_lines[i] << "\n";
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
            std::lock_guard<std::mutex> lock(counter_mutex_);
            for (std::map<std::string, std::unique_ptr<EnhancedCounter> >::const_iterator it = counters_.begin(); 
                 it != counters_.end(); ++it) {
                if (!first_counter) ss << ",\n";
                ss << "    \"" << it->second->name() << "\": " << it->second->value();
                first_counter = false;
            }
        }
        ss << "\n  },\n";
        
        ss << "  \"gauges\": {\n";
        bool first_gauge = true;
        {
            std::lock_guard<std::mutex> lock(gauge_mutex_);
            for (std::map<std::string, std::unique_ptr<EnhancedGauge> >::const_iterator it = gauges_.begin(); 
                 it != gauges_.end(); ++it) {
                if (!first_gauge) ss << ",\n";
                ss << "    \"" << it->second->name() << "\": " << std::fixed << std::setprecision(6) << it->second->value();
                first_gauge = false;
            }
        }
        ss << "\n  },\n";
        
        ss << "  \"histograms\": {\n";
        bool first_hist = true;
        {
            std::lock_guard<std::mutex> lock(histogram_mutex_);
            for (std::map<std::string, std::unique_ptr<EnhancedHistogram> >::const_iterator it = histograms_.begin(); 
                 it != histograms_.end(); ++it) {
                if (!first_hist) ss << ",\n";
                EnhancedHistogram::Stats stats = it->second->get_stats();
                ss << "    \"" << it->second->name() << "\": {"
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
        std::lock_guard<std::mutex> lock1(counter_mutex_);
        std::lock_guard<std::mutex> lock2(gauge_mutex_);
        std::lock_guard<std::mutex> lock3(histogram_mutex_);
        counters_.clear();
        gauges_.clear();
        histograms_.clear();
    }
    
private:
    EnhancedMetricsRegistry() {}
    
    std::string label_key(const Labels& labels) const {
        std::string key;
        for (Labels::const_iterator it = labels.begin(); it != labels.end(); ++it) {
            key += "|" + it->first + "=" + it->second;
        }
        return key;
    }
    
    mutable std::mutex counter_mutex_;
    mutable std::mutex gauge_mutex_;
    mutable std::mutex histogram_mutex_;
    mutable std::mutex rate_limit_mutex_;
    std::map<std::string, std::unique_ptr<EnhancedCounter> > counters_;
    std::map<std::string, std::unique_ptr<EnhancedGauge> > gauges_;
    std::map<std::string, std::unique_ptr<EnhancedHistogram> > histograms_;
    std::map<std::string, std::unique_ptr<RateLimiter> > rate_limiters_;
};

// Convenience functions
inline EnhancedCounter& get_enhanced_counter(const std::string& name, const Labels& labels = Labels()) {
    return EnhancedMetricsRegistry::instance().get_counter(name, labels);
}

inline EnhancedGauge& get_enhanced_gauge(const std::string& name, const Labels& labels = Labels()) {
    return EnhancedMetricsRegistry::instance().get_gauge(name, labels);
}

inline EnhancedHistogram& get_enhanced_histogram(const std::string& name, const Labels& labels = Labels(), 
                                                const std::vector<double>& buckets = EnhancedHistogram::get_default_buckets()) {
    return EnhancedMetricsRegistry::instance().get_histogram(name, labels, buckets);
}

inline bool observe_rate_limited(const std::string& name, double value, const Labels& labels = Labels()) {
    return EnhancedMetricsRegistry::instance().observe_rate_limited(name, value, labels);
}

inline std::string export_enhanced_metrics_prometheus() {
    return EnhancedMetricsRegistry::instance().export_prometheus();
}

inline std::string export_enhanced_metrics_json() {
    return EnhancedMetricsRegistry::instance().export_json();
}

}

#endif // NEURO_OS_UTILS_METRICS_ENHANCED_SIMPLE_HPP