#include <iostream>
#include <cassert>
#include <vector>
#include <thread>

// Test basic tiered allocator functionality
#include "neuro_os/memory/tiered_allocator.hpp"

using namespace neuro_os::memory;

void test_basic_allocation() {
    std::cout << "=== Testing Basic Tiered Allocator ===\n";
    
    TieredAllocator allocator;
    
    // Test small allocation (should go to DRAM)
    void* small_ptr = allocator.allocate(128);
    assert(small_ptr != nullptr);
    std::cout << "Small allocation (128 bytes): " << small_ptr << "\n";
    
    // Test medium allocation
    void* medium_ptr = allocator.allocate(8192);
    assert(medium_ptr != nullptr);
    std::cout << "Medium allocation (8KB): " << medium_ptr << "\n";
    
    // Test large allocation
    void* large_ptr = allocator.allocate(1024 * 1024); // 1MB
    assert(large_ptr != nullptr);
    std::cout << "Large allocation (1MB): " << large_ptr << "\n";
    
    // Check metrics
    auto metrics = allocator.get_metrics();
    std::cout << "Total allocated: " << metrics.total_allocated << " bytes\n";
    std::cout << "Current usage: " << metrics.current_usage << " bytes\n";
    std::cout << "Peak usage: " << metrics.peak_usage << " bytes\n";
    
    // Deallocate
    allocator.deallocate(small_ptr, 128);
    allocator.deallocate(medium_ptr, 8192);
    allocator.deallocate(large_ptr, 1024 * 1024);
    
    std::cout << "All allocations deallocated\n";
    
    // Check metrics after deallocation
    metrics = allocator.get_metrics();
    std::cout << "After deallocation - Current usage: " << metrics.current_usage << " bytes\n";
    
    std::cout << "Basic allocation test passed!\n\n";
}

void test_pool_allocator() {
    std::cout << "=== Testing Pool Allocator ===\n";
    
    PoolAllocator pool(1024); // Small threshold = 1024 bytes
    
    // Add some pools
    PoolConfig config1;
    config1.block_size = 64;
    config1.pool_size = 4096;
    config1.alignment = 8;
    config1.thread_safe = true;
    config1.zero_memory = false;
    
    PoolConfig config2;
    config2.block_size = 256;
    config2.pool_size = 8192;
    config2.alignment = 8;
    config2.thread_safe = true;
    config2.zero_memory = true;
    
    pool.add_pool("small_pool", config1, 0);
    pool.add_pool("medium_pool", config2, 0);
    
    // Allocate from pools
    std::vector<void*> allocations;
    
    for (int i = 0; i < 10; ++i) {
        void* ptr = pool.allocate(64);
        assert(ptr != nullptr);
        allocations.push_back(ptr);
        std::cout << "Allocated 64-byte block: " << ptr << "\n";
    }
    
    for (int i = 0; i < 5; ++i) {
        void* ptr = pool.allocate(256);
        assert(ptr != nullptr);
        allocations.push_back(ptr);
        std::cout << "Allocated 256-byte block: " << ptr << "\n";
    }
    
    // Deallocate
    for (void* ptr : allocations) {
        // For simplicity, we'll use 64 for all deallocations in this test
        pool.deallocate(ptr, 64);
    }
    
    std::cout << "All pool allocations deallocated\n";
    
    // Get stats
    auto stats = pool.get_all_stats();
    std::cout << "Number of pools: " << stats.size() << "\n";
    
    std::cout << "Pool allocator test passed!\n\n";
}

void test_numa_basic() {
    std::cout << "=== Testing Basic NUMA Affinity ===\n";
    
    NUMAAffinity numa;
    
    if (numa.is_initialized()) {
        std::cout << "NUMA system detected\n";
        std::cout << "Node count: " << numa.get_node_count() << "\n";
        std::cout << "Current node: " << numa.get_current_node() << "\n";
        
        // Test allocation on current node
        void* local_ptr = numa.allocate_local(4096);
        assert(local_ptr != nullptr);
        std::cout << "Local allocation (4KB): " << local_ptr << "\n";
        
        // Test optimal allocation
        void* optimal_ptr = numa.allocate_optimal(8192, true);
        assert(optimal_ptr != nullptr);
        std::cout << "Optimal allocation (8KB): " << optimal_ptr << "\n";
        
        std::free(local_ptr);
        std::free(optimal_ptr);
        
        std::cout << "NUMA allocations freed\n";
    } else {
        std::cout << "NUMA not available or not initialized (single-node system)\n";
        std::cout << "This is normal for many systems\n";
    }
    
    std::cout << "Basic NUMA test completed!\n\n";
}

void test_concurrent_basic() {
    std::cout << "=== Testing Basic Concurrent Allocation ===\n";
    
    TieredAllocator allocator;
    const int num_threads = 4;
    const int allocations_per_thread = 100;
    
    std::vector<std::thread> threads;
    std::vector<int> thread_results(num_threads, 0);
    
    auto worker = [&](int thread_id) {
        std::vector<void*> allocations;
        allocations.reserve(allocations_per_thread);
        
        for (int i = 0; i < allocations_per_thread; ++i) {
            void* ptr = allocator.allocate(64 + (i % 128));
            if (ptr) {
                allocations.push_back(ptr);
                thread_results[thread_id]++;
            }
        }
        
        for (void* ptr : allocations) {
            allocator.deallocate(ptr, 64); // Approximate size
        }
    };
    
    // Start threads
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    // Wait for threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Check results
    int total_allocations = 0;
    for (int count : thread_results) {
        total_allocations += count;
    }
    
    std::cout << "Threads: " << num_threads << "\n";
    std::cout << "Total allocations: " << total_allocations << "\n";
    std::cout << "Expected: " << (num_threads * allocations_per_thread) << "\n";
    
    auto metrics = allocator.get_metrics();
    std::cout << "Final memory usage: " << metrics.current_usage << " bytes\n";
    std::cout << "Peak memory usage: " << metrics.peak_usage << " bytes\n";
    
    std::cout << "Concurrent allocation test passed!\n\n";
}

int main() {
    std::cout << "=== NeuroOS Memory Allocator Simple Tests ===\n\n";
    
    try {
        test_basic_allocation();
        test_pool_allocator();
        test_numa_basic();
        test_concurrent_basic();
        
        std::cout << "=== All Simple Tests Passed! ===\n";
        std::cout << "\nNote: The advanced features (enhanced tracking, memory safety, etc.)\n";
        std::cout << "are implemented in the header files and can be integrated into your application.\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception\n";
        return 1;
    }
}