#include "neuro_os/utils/metrics_enhanced_simple.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace neuro_os::utils;

int main() {
    std::cout << "Testing Enhanced Metrics...\n";
    
    // Test counter with labels
    EnhancedCounter& counter1 = get_enhanced_counter("requests_total", {{"method", "GET"}, {"status", "200"}});
    counter1.increment(5);
    std::cout << "Counter value: " << counter1.value() << std::endl;
    
    // Test gauge with labels
    EnhancedGauge& gauge1 = get_enhanced_gauge("temperature", {{"sensor", "cpu"}, {"unit", "celsius"}});
    gauge1.set(42.5);
    gauge1.increment(2.5);
    std::cout << "Gauge value: " << gauge1.value() << std::endl;
    
    // Test histogram with custom buckets
    std::vector<double> custom_buckets;
    custom_buckets.push_back(0.1);
    custom_buckets.push_back(0.5);
    custom_buckets.push_back(1.0);
    custom_buckets.push_back(2.0);
    
    EnhancedHistogram& hist1 = get_enhanced_histogram("request_duration", {{"service", "api"}}, custom_buckets);
    hist1.observe(0.15);
    hist1.observe(0.3);
    hist1.observe(0.8);
    hist1.observe(1.5);
    hist1.observe(2.5);
    
    EnhancedHistogram::Stats stats = hist1.get_stats();
    std::cout << "Histogram stats - count: " << stats.count 
              << ", sum: " << stats.sum 
              << ", mean: " << stats.mean 
              << ", min: " << stats.min 
              << ", max: " << stats.max 
              << ", stddev: " << stats.stddev << std::endl;
    
    // Test rate limiting
    std::cout << "\nTesting rate limiting (should allow first 2, reject next 3):\n";
    for (int i = 0; i < 5; ++i) {
        bool allowed = observe_rate_limited("high_freq_metric", 1.0, {{"test", "rate_limit"}});
        std::cout << "Observation " << i + 1 << ": " << (allowed ? "ALLOWED" : "RATE LIMITED") << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Wait for rate limiter to reset
    std::cout << "\nWaiting for rate limiter reset...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Test again after reset
    bool allowed = observe_rate_limited("high_freq_metric", 1.0, {{"test", "rate_limit"}});
    std::cout << "After reset: " << (allowed ? "ALLOWED" : "RATE LIMITED") << std::endl;
    
    // Export metrics
    std::cout << "\nPrometheus format:\n";
    std::cout << export_enhanced_metrics_prometheus() << std::endl;
    
    std::cout << "\nJSON format:\n";
    std::cout << export_enhanced_metrics_json() << std::endl;
    
    std::cout << "\nAll enhanced metrics tests completed!\n";
    return 0;
}