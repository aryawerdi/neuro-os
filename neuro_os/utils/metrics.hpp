#ifndef NEURO_OS_UTILS_METRICS_HPP
#define NEURO_OS_UTILS_METRICS_HPP

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace neuro_os::utils {

class Counter {
public:
    Counter() = default;
    explicit Counter(const std::string& name) : name_(name) {}

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
    void set_name(const std::string& name) { name_ = name; }

private:
    std::string name_;
    std::atomic<uint64_t> value_{0};
};

class Gauge {
public:
    Gauge() = default;
    explicit Gauge(const std::string& name) : name_(name), value_(0.0) {}

    void set(double value) {
        value_.store(value, std::memory_order_relaxed);
    }

    void increment(double value = 1.0) {
        double current = value_.load(std::memory_order_relaxed);
        while (!value_.compare_exchange_weak(current, current + value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    void decrement(double value = 1.0) {
        double current = value_.load(std::memory_order_relaxed);
        while (!value_.compare_exchange_weak(current, current - value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    double value() const {
        return value_.load(std::memory_order_relaxed);
    }

    const std::string& name() const { return name_; }
    void set_name(const std::string& name) { name_ = name; }

private:
    std::string name_;
    std::atomic<double> value_{0.0};
};

class Histogram {
public:
    struct Bucket {
        double upper_bound;
        uint64_t count;
    };

    Histogram() = default;
    explicit Histogram(const std::string& name) : name_(name) {
        setup_default_buckets();
    }

    explicit Histogram(const std::string& name, const std::vector<double>& bounds)
        : name_(name), custom_bounds_(bounds) {
        setup_buckets_from_bounds();
    }

    void observe(double value) {
        double current_total = total_.load(std::memory_order_relaxed);
        while (!total_.compare_exchange_weak(current_total, current_total + value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
        
        count_.fetch_add(1, std::memory_order_relaxed);

        for (auto& bucket : buckets_) {
            if (value <= bucket.upper_bound) {
                bucket.count++;
                break;
            }
        }
    }

    void reset() {
        total_.store(0.0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
        for (auto& bucket : buckets_) {
            bucket.count = 0;
        }
    }

    struct Stats {
        double count;
        double sum;
        double mean;
        double min;
        double max;
        double stddev;
        double p50;
        double p90;
        double p95;
        double p99;
    };

    Stats get_stats() const {
        Stats stats;
        stats.count = static_cast<double>(count_.load(std::memory_order_relaxed));
        stats.sum = total_.load(std::memory_order_relaxed);
        stats.mean = stats.count > 0 ? stats.sum / stats.count : 0.0;
        stats.min = 0.0;
        stats.max = 0.0;
        stats.stddev = 0.0;
        stats.p50 = 0.0;
        stats.p90 = 0.0;
        stats.p95 = 0.0;
        stats.p99 = 0.0;
        return stats;
    }

    const std::string& name() const { return name_; }
    void set_name(const std::string& name) { name_ = name; }

private:
    void setup_default_buckets() {
        custom_bounds_ = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
        setup_buckets_from_bounds();
    }

    void setup_buckets_from_bounds() {
        buckets_.clear();
        for (double bound : custom_bounds_) {
            buckets_.push_back({bound, 0});
        }
    }

    std::string name_;
    std::vector<double> custom_bounds_;
    std::vector<Bucket> buckets_;
    std::atomic<uint64_t> count_{0};
    std::atomic<double> total_{0.0};
};

class MetricsRegistry {
public:
    static MetricsRegistry& instance() {
        static MetricsRegistry instance;
        return instance;
    }

    Counter& get_counter(const std::string& name) {
        {
            std::shared_lock lock(mutex_);
            auto it = counters_.find(name);
            if (it != counters_.end()) {
                return *it->second;
            }
        }
        {
            std::unique_lock lock(mutex_);
            if (counters_.find(name) == counters_.end()) {
                counters_[name] = std::make_unique<Counter>(name);
            }
            return *counters_[name];
        }
    }

    Gauge& get_gauge(const std::string& name) {
        {
            std::shared_lock lock(mutex_);
            auto it = gauges_.find(name);
            if (it != gauges_.end()) {
                return *it->second;
            }
        }
        {
            std::unique_lock lock(mutex_);
            if (gauges_.find(name) == gauges_.end()) {
                gauges_[name] = std::make_unique<Gauge>(name);
            }
            return *gauges_[name];
        }
    }

    Histogram& get_histogram(const std::string& name) {
        {
            std::shared_lock lock(mutex_);
            auto it = histograms_.find(name);
            if (it != histograms_.end()) {
                return *it->second;
            }
        }
        {
            std::unique_lock lock(mutex_);
            if (histograms_.find(name) == histograms_.end()) {
                histograms_[name] = std::make_unique<Histogram>(name);
            }
            return *histograms_[name];
        }
    }

    Histogram& get_histogram(const std::string& name, const std::vector<double>& bounds) {
        std::unique_lock lock(mutex_);
        if (histograms_.find(name) == histograms_.end()) {
            histograms_[name] = std::make_unique<Histogram>(name, bounds);
        }
        return *histograms_[name];
    }

    std::string export_json() const {
        std::stringstream ss;
        ss << "{\n";

        ss << "  \"counters\": {\n";
        bool first = true;
        for (const auto& [name, counter] : counters_) {
            if (!first) ss << ",\n";
            ss << "    \"" << name << "\": " << counter->value();
            first = false;
        }
        ss << "\n  },\n";

        ss << "  \"gauges\": {\n";
        first = true;
        for (const auto& [name, gauge] : gauges_) {
            if (!first) ss << ",\n";
            ss << "    \"" << name << "\": " << std::fixed << std::setprecision(6) << gauge->value();
            first = false;
        }
        ss << "\n  },\n";

        ss << "  \"histograms\": {\n";
        first = true;
        for (const auto& [name, histogram] : histograms_) {
            if (!first) ss << ",\n";
            auto stats = histogram->get_stats();
            ss << "    \"" << name << "\": {"
               << "\"count\": " << stats.count << ", "
               << "\"sum\": " << stats.sum << ", "
               << "\"mean\": " << stats.mean << "}";
            first = false;
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
    MetricsRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::map<std::string, std::unique_ptr<Counter>> counters_;
    std::map<std::string, std::unique_ptr<Gauge>> gauges_;
    std::map<std::string, std::unique_ptr<Histogram>> histograms_;
};

inline Counter& get_counter(const std::string& name) {
    return MetricsRegistry::instance().get_counter(name);
}

inline Gauge& get_gauge(const std::string& name) {
    return MetricsRegistry::instance().get_gauge(name);
}

inline Histogram& get_histogram(const std::string& name) {
    return MetricsRegistry::instance().get_histogram(name);
}

inline Histogram& get_histogram(const std::string& name, const std::vector<double>& bounds) {
    return MetricsRegistry::instance().get_histogram(name, bounds);
}

inline std::string export_metrics_json() {
    return MetricsRegistry::instance().export_json();
}

}

#endif
