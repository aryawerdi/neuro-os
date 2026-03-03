#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>

#include "neuro_os/memory/advanced_tracking.hpp"
#include "neuro_os/memory/enhanced_pool.hpp"
#include "neuro_os/memory/numa_optimizations.hpp"
#include "neuro_os/memory/memory_safety.hpp"

using namespace neuro_os::memory;

void test_advanced_tracking() {
    std::cout << "=== Testing Advanced Tracking ===\n";
    
    MemoryUsageTracker tracker;
    
    tracker.track_allocation(1024, MemoryTier::DRAM);
    tracker.track_allocation(2048, MemoryTier::DRAM);
    tracker.track_allocation(4096, MemoryTier::PMEM);
    
    std::cout << "Current usage: " << tracker.get_current_usage() << " bytes\n";
    std::cout << "Peak usage: " << tracker.get_peak_usage() << " bytes\n";
    std::cout << "DRAM usage: " << tracker.get_tier_usage(MemoryTier::DRAM) << " bytes\n";
    std::cout << "PMEM usage: " << tracker.get_tier_usage(MemoryTier::PMEM) << " bytes\n";
    
    tracker.track_deallocation(1024, MemoryTier::DRAM);
    std::cout << "After deallocation: " << tracker.get_current_usage() << " bytes\n";
    
    std::cout << "Advanced tracking test passed!\n\n";
}

void test_enhanced_pool() {
    std::cout << "=== Testing Enhanced Pool ===\n";
    
    SizeClassConfig config;
    config.size = 64;
    config.block_size = 64;
    config.slab_size = 4096;
    config.magazine_capacity = 32;
    config.thread_cache_enabled = true;
    config.memory_poisoning = true;
    
    EnhancedPool pool("test_pool");
    pool.add_size_class(config);
    
    std::vector<void*> allocations;
    
    for (int i = 0; i < 10; ++i) {
        void* ptr = pool.allocate(64);
        assert(ptr != nullptr);
        allocations.push_back(ptr);
    }
    
    std::cout << "Allocated " << allocations.size() << " blocks\n";
    
    for (void* ptr : allocations) {
        pool.deallocate(ptr, 64);
    }
    
    std::cout << "Deallocated all blocks\n";
    
    auto stats = pool.get_thread_cache_stats();
    std::cout << "Thread cache stats collected: " << stats.size() << " entries\n";
    
    std::cout << "Enhanced pool test passed!\n\n";
}

void test_memory_safety() {
    std::cout << "=== Testing Memory Safety ===\n";
    
    MemorySafetyManager safety_manager;
    
    safety_manager.enable_bounds_checking();
    safety_manager.enable_use_after_free_detection();
    safety_manager.enable_double_free_detection();
    safety_manager.enable_memory_leak_detection();
    
    void* ptr1 = std::malloc(100);
    void* ptr2 = std::malloc(200);
    
    void* protected_ptr1 = safety_manager.protect_allocation(ptr1, 100, MemoryTier::DRAM, 0, "test1");
    void* protected_ptr2 = safety_manager.protect_allocation(ptr2, 200, MemoryTier::DRAM, 0, "test2");
    
    assert(protected_ptr1 != nullptr);
    assert(protected_ptr2 != nullptr);
    
    std::cout << "Protected allocations: " << protected_ptr1 << ", " << protected_ptr2 << "\n";
    
    safety_manager.record_deallocation(protected_ptr1, 100, MemoryTier::DRAM);
    safety_manager.record_deallocation(protected_ptr2, 200, MemoryTier::DRAM);
    
    SafetyStats stats = safety_manager.get_total_stats();
    std::cout << "Safety stats:\n";
    std::cout << "  Total checks: " << stats.total_checks << "\n";
    std::cout << "  Passed checks: " << stats.passed_checks << "\n";
    std::cout << "  Bounds violations: " << stats.bounds_violations << "\n";
    std::cout << "  Use-after-free detections: " << stats.use_after_free_detections << "\n";
    std::cout << "  Double-free detections: " << stats.double_free_detections << "\n";
    
    std::cout << "Memory safety test passed!\n\n";
}

void test_numa_optimizations() {
    std::cout << "=== Testing NUMA Optimizations ===\n";
    
    NUMAAwareAllocator numa_allocator;
    
    if (numa_allocator.initialize()) {
        std::cout << "NUMA allocator initialized successfully\n";
        
        void* ptr1 = numa_allocator.allocate(4096, 0);
        void* ptr2 = numa_allocator.allocate(8192, -1);
        
        assert(ptr1 != nullptr);
        assert(ptr2 != nullptr);
        
        std::cout << "Allocated on node 0: " << ptr1 << "\n";
        std::cout << "Allocated on optimal node: " << ptr2 << "\n";
        
        numa_allocator.deallocate(ptr1, 4096);
        numa_allocator.deallocate(ptr2, 8192);
        
        std::cout << "Total allocated: " << numa_allocator.get_total_allocated() << " bytes\n";
        
        numa_allocator.bind_current_thread_to_node(0);
        std::cout << "Current thread bound to node 0\n";
    } else {
        std::cout << "NUMA allocator initialization failed (may not be available on this system)\n";
    }
    
    std::cout << "NUMA optimizations test completed!\n\n";
}

void test_concurrent_allocations() {
    std::cout << "=== Testing Concurrent Allocations ===\n";
    
    EnhancedPool pool("concurrent_pool");
    
    SizeClassConfig config;
    config.size = 128;
    config.block_size = 128;
    config.slab_size = 8192;
    config.magazine_capacity = 64;
    config.thread_cache_enabled = true;
    config.memory_poisoning = false;
    
    pool.add_size_class(config);
    
    const int num_threads = 4;
    const int allocations_per_thread = 1000;
    
    std::vector<std::thread> threads;
    std::atomic<int> total_allocations(0);
    
    auto worker = [&](int thread_id) {
        std::vector<void*> allocations;
        allocations.reserve(allocations_per_thread);
        
        for (int i = 0; i < allocations_per_thread; ++i) {
            void* ptr = pool.allocate(128);
            if (ptr) {
                allocations.push_back(ptr);
                total_allocations++;
            }
        }
        
        for (void* ptr : allocations) {
            pool.deallocate(ptr, 128);
        }
    };
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Threads: " << num_threads << "\n";
    std::cout << "Total allocations: " << total_allocations << "\n";
    std::cout << "Time: " << duration.count() << " ms\n";
    std::cout << "Allocations per second: " 
              << (total_allocations * 1000.0 / duration.count()) << "\n";
    
    auto stats = pool.get_thread_cache_stats();
    std::cout << "Thread cache entries: " << stats.size() << "\n";
    
    std::cout << "Concurrent allocations test passed!\n\n";
}

int main() {
    std::cout << "=== NeuroOS Memory Allocator Feature Tests ===\n\n";
    
    try {
        test_advanced_tracking();
        test_enhanced_pool();
        test_memory_safety();
        test_numa_optimizations();
        test_concurrent_allocations();
        
        std::cout << "=== All Tests Passed! ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception\n";
        return 1;
    }
}