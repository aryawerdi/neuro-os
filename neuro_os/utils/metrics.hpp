#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <mutex>
#include <vector>
#include <atomic>
#include <functional>
#include <map>
#include <numeric>
#include <algorithm>
#include <cmath>

namespace neuro_os {

struct MetricValue {
    double value;
    std::chrono::steady_clock::time_point timestamp;
    std::map<std::string, std::string> tags;
    
    MetricValue() : value(0), timestamp(std::chrono::steady_clock::now()) {}
    explicit MetricValue(double v) : value(v), timestamp(std::chrono::steady_clock::now()) {}
    MetricValue(double v, std::map<std::string, std::string> t) 
        : value(v), timestamp(std::chrono::steady_clock::now()), tags(std::move(t)) {}
};

class MetricsCollector {
public:
    using MetricCallback = std::function<void(const std::string&, const MetricValue&)>;
    
    void record(const std::string& name, double value);
    void record(const std::string& name, double value, const std::map<std::string, std::string>& tags);
    
    double get(const std::string& name) const;
    double get_sum(const std::string& name) const;
    double get_avg(const std::string& name) const;
    double get_count(const std::string& name) const;
    double get_min(const std::string& name) const;
    double get_max(const std::string& name) const;
    double get_percentile(double p) const;
    
    void register_callback(MetricCallback callback);
    void clear();
    
    std::vector<std::string> get_metric_names() const;
    size_t get_sample_count(const std::string& name) const;

private:
    std::map<std::string, std::vector<MetricValue>> metrics_;
    std::vector<MetricCallback> callbacks_;
    mutable std::mutex mutex_;
};

class Counter {
public:
    explicit Counter(const std::string& name) : name_(name), value_(0) {}
    
    void increment() { ++value_; }
    void increment(double delta) { value_ += delta; }
    void reset() { value_ = 0; }
    
    double value() const { return value_.load(); }
    std::string name() const { return name_; }
    
private:
    std::string name_;
    std::atomic<double> value_;
};

class Gauge {
public:
    explicit Gauge(const std::string& name) : name_(name), value_(0) {}
    
    void set(double value) { value_.store(value); }
    void increment() { ++value_; }
    void decrement() { --value_; }
    void add(double delta) { value_ += delta; }
    void subtract(double delta) { value_ -= delta; }
    
    double value() const { return value_.load(); }
    std::string name() const { return name_; }
    
private:
    std::string name_;
    std::atomic<double> value_;
};

class Histogram {
public:
    explicit Histogram(const std::string& name) : name_(name) {}
    
    void record(double value);
    void record(double value, const std::map<std::string, std::string>& tags);
    
    double min() const;
    double max() const;
    double mean() const;
    double stddev() const;
    double percentile(double p) const;
    size_t count() const;
    
    std::string name() const { return name_; }
    const std::vector<double>& get_values() const { return values_; }
    
private:
    std::string name_;
    std::vector<double> values_;
    mutable std::mutex mutex_;
};

class Timer {
public:
    explicit Timer(const std::string& name) : name_(name), start_(std::chrono::steady_clock::now()) {}
    
    void stop();
    void reset();
    
    double elapsed_ns() const;
    double elapsed_us() const;
    double elapsed_ms() const;
    double elapsed_s() const;
    
    std::string name() const { return name_; }
    
private:
    std::string name_;
    std::chrono::steady_clock::time_point start_;
    bool stopped_ = false;
};

class MetricsRegistry {
public:
    static MetricsRegistry& instance();
    
    Counter& get_counter(const std::string& name);
    Gauge& get_gauge(const std::string& name);
    Histogram& get_histogram(const std::string& name);
    
    MetricsCollector& collector() { return collector_; }
    
    void clear();
    
private:
    MetricsRegistry() = default;
    
    std::map<std::string, std::unique_ptr<Counter>> counters_;
    std::map<std::string, std::unique_ptr<Gauge>> gauges_;
    std::map<std::string, std::unique_ptr<Histogram>> histograms_;
    MetricsCollector collector_;
    std::mutex mutex_;
};

}
