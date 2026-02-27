#include "neuro_os/utils/logging.hpp"
#include "neuro_os/utils/metrics.hpp"
#include "neuro_os/utils/thread_pool.hpp"
#include "neuro_os/utils/config.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace neuro_os::utils;

int main() {
    std::cout << "Testing NeuroOS Utils...\n";

    Logger::instance().set_level(LogLevel::DEBUG);
    NEURO_OS_LOG_INFO("Starting NeuroOS Utils test");
    
    auto& counter = get_counter("test_counter");
    counter.increment(5);
    std::cout << "Counter value: " << counter.value() << std::endl;
    
    auto& gauge = get_gauge("test_gauge");
    gauge.set(42.5);
    std::cout << "Gauge value: " << gauge.value() << std::endl;
    
    auto& hist = get_histogram("test_histogram");
    hist.observe(0.05);
    hist.observe(0.1);
    hist.observe(0.5);
    auto stats = hist.get_stats();
    std::cout << "Histogram count: " << stats.count << ", mean: " << stats.mean << std::endl;

    ThreadPool pool(2);
    auto f1 = pool.enqueue([]() { 
        std::cout << "Task 1 running\n"; 
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 1;
    });
    auto f2 = pool.enqueue([]() { 
        std::cout << "Task 2 running\n"; 
        return 2;
    });
    std::cout << "Task 1 result: " << f1.get() << std::endl;
    std::cout << "Task 2 result: " << f2.get() << std::endl;
    
    pool.wait_all();
    pool.shutdown();

    Config config;
    config.set("name", std::string("NeuroOS"));
    config.set("version", int64_t(1));
    config.set("debug", bool(true));
    
    std::string json = config.dump_json();
    std::cout << "Config JSON:\n" << json << std::endl;
    
    std::string yaml = config.dump_yaml();
    std::cout << "Config YAML:\n" << yaml << std::endl;

    auto name = config.get<std::string>("name");
    if (name) {
        std::cout << "Name: " << *name << std::endl;
    }

    std::cout << "Export JSON:\n" << export_metrics_json() << std::endl;

    NEURO_OS_LOG_INFO("NeuroOS Utils test completed");
    
    std::cout << "All tests passed!\n";
    return 0;
}
