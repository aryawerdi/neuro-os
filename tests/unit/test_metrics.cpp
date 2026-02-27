#include <gtest/gtest.h>
#include "neuro_os/utils/metrics.hpp"
#include <thread>
#include <vector>
#include <cmath>

using namespace neuro_os;

TEST(MetricsTest, RecordBasicMetric) {
    MetricsCollector collector;
    collector.record("test_metric", 42.0);
    
    EXPECT_EQ(collector.get("test_metric"), 42.0);
}

TEST(MetricsTest, RecordMultipleValues) {
    MetricsCollector collector;
    collector.record("test_metric", 10.0);
    collector.record("test_metric", 20.0);
    collector.record("test_metric", 30.0);
    
    EXPECT_EQ(collector.get("test_metric"), 30.0);
    EXPECT_EQ(collector.get_count("test_metric"), 3.0);
    EXPECT_NEAR(collector.get_avg("test_metric"), 20.0, 0.001);
}

TEST(MetricsTest, GetMinMax) {
    MetricsCollector collector;
    collector.record("test_metric", 10.0);
    collector.record("test_metric", 50.0);
    collector.record("test_metric", 30.0);
    
    EXPECT_EQ(collector.get_min("test_metric"), 10.0);
    EXPECT_EQ(collector.get_max("test_metric"), 50.0);
}

TEST(MetricsTest, CounterOperations) {
    Counter counter("test_counter");
    
    EXPECT_EQ(counter.value(), 0);
    counter.increment();
    EXPECT_EQ(counter.value(), 1);
    counter.increment(5);
    EXPECT_EQ(counter.value(), 6);
    counter.reset();
    EXPECT_EQ(counter.value(), 0);
}

TEST(MetricsTest, GaugeOperations) {
    Gauge gauge("test_gauge");
    
    EXPECT_EQ(gauge.value(), 0);
    gauge.set(100.0);
    EXPECT_EQ(gauge.value(), 100);
    gauge.increment();
    EXPECT_EQ(gauge.value(), 101);
    gauge.decrement();
    EXPECT_EQ(gauge.value(), 100);
    gauge.add(50.0);
    EXPECT_EQ(gauge.value(), 150);
    gauge.subtract(50.0);
    EXPECT_EQ(gauge.value(), 100);
}

TEST(MetricsTest, HistogramOperations) {
    Histogram histogram("test_histogram");
    
    histogram.record(10.0);
    histogram.record(20.0);
    histogram.record(30.0);
    histogram.record(40.0);
    histogram.record(50.0);
    
    EXPECT_EQ(histogram.count(), 5);
    EXPECT_EQ(histogram.min(), 10.0);
    EXPECT_EQ(histogram.max(), 50.0);
    EXPECT_NEAR(histogram.mean(), 30.0, 0.001);
}

TEST(MetricsTest, HistogramPercentile) {
    Histogram histogram("test_histogram");
    
    for (int i = 1; i <= 100; ++i) {
        histogram.record(static_cast<double>(i));
    }
    
    EXPECT_NEAR(histogram.percentile(50), 50.5, 1.0);
    EXPECT_NEAR(histogram.percentile(90), 90.5, 1.0);
    EXPECT_NEAR(histogram.percentile(99), 99.5, 1.0);
}

TEST(MetricsTest, TimerOperations) {
    Timer timer("test_timer");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.stop();
    
    double elapsed_ms = timer.elapsed_ms();
    EXPECT_GE(elapsed_ms, 9.0);
    EXPECT_LE(elapsed_ms, 100.0);
}

TEST(MetricsTest, ConcurrentRecording) {
    MetricsCollector collector;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&collector, i]() {
            for (int j = 0; j < 100; ++j) {
                collector.record("concurrent_metric", static_cast<double>(i * 100 + j));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(collector.get_sample_count("concurrent_metric"), 1000);
}

TEST(MetricsTest, MetricWithTags) {
    MetricsCollector collector;
    std::map<std::string, std::string> tags1{{"env", "test"}, {"region", "us-east"}};
    std::map<std::string, std::string> tags2{{"env", "test"}, {"region", "us-west"}};
    
    collector.record("tagged_metric", 10.0, tags1);
    collector.record("tagged_metric", 20.0, tags2);
    
    EXPECT_EQ(collector.get_sample_count("tagged_metric"), 2);
}
