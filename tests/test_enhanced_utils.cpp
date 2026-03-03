#include "neuro_os/utils/logging_simple_enhanced.hpp"
#include "neuro_os/utils/metrics_enhanced_simple.hpp"
#include "neuro_os/utils/thread_pool_enhanced_simple.hpp"
#include "neuro_os/utils/config_enhanced.hpp"

#include <iostream>
#include <chrono>
#include <thread>

int main() {
    std::cout << "Testing Enhanced NeuroOS Utilities\n";
    std::cout << "==================================\n\n";
    
    // Test 1: Enhanced Logging
    std::cout << "1. Testing Enhanced Logging:\n";
    {
        neuro_os::utils::EnhancedLogger& logger = neuro_os::utils::EnhancedLogger::instance();
        logger.set_level(neuro_os::utils::LogLevel::INFO);
        
        // Use logging macros
        NEURO_OS_LOG_INFO("Test info message");
        NEURO_OS_LOG_WARN("Test warning message");
        NEURO_OS_LOG_ERROR("Test error message");
        
        // Test with context (set context fields on logger)
        logger.set_context_field("user_id", "12345");
        logger.set_context_field("request_id", "req-abc");
        NEURO_OS_LOG_INFO("User action completed");
        
        std::cout << "   Logging test completed\n";
    }
    
    // Test 2: Enhanced Metrics
    std::cout << "\n2. Testing Enhanced Metrics:\n";
    {
        // Test counter with labels
        neuro_os::utils::EnhancedCounter& request_counter = neuro_os::utils::get_enhanced_counter(
            "http_requests_total",
            {{"method", "GET"}, {"path", "/api/users"}}
        );
        request_counter.increment();
        request_counter.increment(2);
        
        // Test gauge
        neuro_os::utils::EnhancedGauge& memory_gauge = neuro_os::utils::get_enhanced_gauge(
            "memory_usage_bytes",
            {{"type", "heap"}}
        );
        memory_gauge.set(1024 * 1024 * 100); // 100MB
        memory_gauge.increment(1024 * 1024); // Add 1MB
        
        // Test histogram
        neuro_os::utils::EnhancedHistogram& latency_histogram = neuro_os::utils::get_enhanced_histogram(
            "http_request_duration_seconds",
            {{"method", "GET"}}
        );
        
        for (int i = 0; i < 100; ++i) {
            double latency = 0.01 + (rand() % 100) / 1000.0;
            latency_histogram.observe(latency);
        }
        
        // Test rate limiting
        for (int i = 0; i < 10; ++i) {
            bool recorded = neuro_os::utils::observe_rate_limited(
                "high_frequency_events",
                1.0,
                {{"source", "test"}}
            );
            if (!recorded) {
                std::cout << "   Rate limiting blocked event " << i << "\n";
            }
        }
        
        // Export metrics
        std::string prometheus = neuro_os::utils::export_enhanced_metrics_prometheus();
        std::cout << "   Prometheus metrics exported (" << prometheus.size() << " bytes)\n";
        
        std::string json = neuro_os::utils::export_enhanced_metrics_json();
        std::cout << "   JSON metrics exported (" << json.size() << " bytes)\n";
    }
    
    // Test 3: Enhanced Thread Pool
    std::cout << "\n3. Testing Enhanced Thread Pool:\n";
    {
        auto pool = neuro_os::utils::create_enhanced_thread_pool_simple(4);
        
        // Submit tasks with different priorities
        auto future1 = pool->submit_with_priority(
            neuro_os::utils::TaskPriority::HIGH,
            []() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return "High priority task completed";
            }
        );
        
        auto future2 = pool->submit_with_priority(
            neuro_os::utils::TaskPriority::LOW,
            []() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                return "Low priority task completed";
            }
        );
        
        // Submit task with timeout
        auto future3 = pool->submit_with_timeout(
            std::chrono::milliseconds(200),
            []() {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                return "Task with timeout check";
            }
        );
        
        // Get results
        std::cout << "   " << future1.get() << "\n";
        std::cout << "   " << future2.get() << "\n";
        std::cout << "   " << future3.get() << "\n";
        
        // Enable work stealing
        pool->enable_work_stealing(true);
        
        // Get statistics
        auto stats = pool->get_statistics();
        std::cout << "   Thread pool statistics:\n";
        std::cout << "     Threads: " << pool->get_thread_count() << "\n";
        std::cout << "     Tasks submitted: " << stats.total_tasks_submitted << "\n";
        std::cout << "     Tasks completed: " << stats.total_tasks_completed << "\n";
        std::cout << "     Tasks in queue: " << stats.tasks_in_queue << "\n";
        
        // Wait for all tasks
        pool->wait_for_all();
        std::cout << "   All tasks completed\n";
    }
    
    // Test 4: Enhanced Configuration
    std::cout << "\n4. Testing Enhanced Configuration:\n";
    {
        neuro_os::utils::EnhancedConfig& config = neuro_os::utils::get_enhanced_config();
        
        // Set configuration values
        config.set("server.port", neuro_os::utils::ConfigValue(8080));
        config.set("server.host", neuro_os::utils::ConfigValue("localhost"));
        config.set("debug.enabled", neuro_os::utils::ConfigValue(true));
        config.set("database.timeout", neuro_os::utils::ConfigValue(30.5));
        
        // Mark some values as secrets
        config.mark_as_secret("database.password");
        config.set("database.password", neuro_os::utils::ConfigValue("secret123"));
        
        // Get values
        std::cout << "   Server: " << config.get_string("server.host") 
                  << ":" << config.get_int("server.port") << "\n";
        std::cout << "   Debug enabled: " << (config.get_bool("debug.enabled") ? "yes" : "no") << "\n";
        std::cout << "   Database timeout: " << config.get_double("database.timeout") << "s\n";
        std::cout << "   Database password: " << config.get_masked("database.password") << "\n";
        
        // Test environment variable interpolation
        setenv("APP_HOME", "/opt/myapp", 1);
        config.set("app.path", neuro_os::utils::ConfigValue("${APP_HOME}/data"));
        std::cout << "   App path (with env var): " << config.get_string("app.path") << "\n";
        
        // Register change callback
        config.register_change_callback("debug.enabled", 
            [](const std::string& key, const neuro_os::utils::ConfigValue& old_val, 
               const neuro_os::utils::ConfigValue& new_val) {
                std::cout << "   Config changed: " << key << " = " << new_val.as_string() << "\n";
            }
        );
        
        // Trigger change
        config.set("debug.enabled", neuro_os::utils::ConfigValue(false));
        
        // Set schema and validate
        neuro_os::utils::ConfigSchema schema;
        schema.add_field({"server.port", neuro_os::utils::ConfigValue::Type::INT, 
                         neuro_os::utils::ConfigValue(8080), true, "Server port number"});
        schema.add_field({"server.host", neuro_os::utils::ConfigValue::Type::STRING, 
                         neuro_os::utils::ConfigValue("localhost"), false, "Server hostname"});
        
        config.set_schema(schema);
        
        std::string error;
        if (config.validate(error)) {
            std::cout << "   Configuration validation passed\n";
        } else {
            std::cout << "   Configuration validation failed: " << error << "\n";
        }
        
        // Export configuration
        std::cout << "   Configuration export:\n" << config.export_as_string() << "\n";
        
        // Test hot reload (simulated)
        config.start_hot_reload_watcher(std::chrono::milliseconds(100));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        config.stop_hot_reload_watcher();
        std::cout << "   Hot reload watcher tested\n";
    }
    
    std::cout << "\nAll enhanced utility tests completed successfully!\n";
    return 0;
}